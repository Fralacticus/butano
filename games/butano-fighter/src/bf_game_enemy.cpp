#include "bf_game_enemy.h"

#include "btn_sprite_builder.h"
#include "btn_sprite_affine_mats.h"
#include "btn_sprite_items_enemy_explosion.h"
#include "bf_game_hero.h"
#include "bf_game_objects.h"
#include "bf_game_enemy_event.h"
#include "bf_game_enemy_bullets.h"
#include "bf_game_check_hero_bullet_data.h"

namespace bf::game
{

namespace
{
    constexpr const int damage_frames = 12;

    btn::sprite_ptr _create_sprite(const enemy_event& event)
    {
        const enemy_data& enemy_data = event.enemy;
        int animation_index = event.move_events[0].animation_index;
        btn::sprite_builder builder(enemy_data.sprite_item, enemy_data.graphics_indexes_groups[animation_index][0]);
        builder.set_position(event.start_position);
        builder.set_z_order(constants::enemies_z_order);
        builder.set_horizontal_flip(event.move_events[0].horizontal_flip);
        return builder.release_build();
    }

    btn::sprite_animate_action<4> _create_animate_action(const btn::sprite_ptr& sprite, const enemy_data& data,
                                                         int animation_index)
    {
        return btn::sprite_animate_action<4>::forever(
                    sprite, data.animation_wait_frames, data.sprite_item.tiles_item(),
                    data.graphics_indexes_groups[animation_index]);
    }
}

enemy::enemy(const enemy_event& event, const btn::sprite_palette_ptr& damage_palette, int8_t tag) :
    _event(&event),
    _sprite(_create_sprite(event)),
    _move_action(_sprite, event.move_events[0].delta_position),
    _animate_action(_create_animate_action(_sprite, event.enemy, event.move_events[0].animation_index)),
    _sprite_palette(_sprite.palette()),
    _damage_palette(damage_palette),
    _life(event.enemy.life),
    _move_event_counter(event.move_events[0].duration_frames),
    _bullet_event_counter(0),
    _move_event_index(0),
    _bullet_event_index(0),
    _grid_columns(int8_t(btn::max(event.enemy.dimensions.width().integer() / constants::enemies_grid_size, 1))),
    _grid_rows(int8_t(btn::max(event.enemy.dimensions.height().integer() / constants::enemies_grid_size, 1))),
    _last_grid_column(0),
    _last_grid_row(0),
    _damage_palette_counter(0),
    _ignore_hero_bullet_counter(constants::enemies_invencible_frames),
    _tag(tag)
{
    if(! event.bullet_events.empty())
    {
        _bullet_event_counter = event.bullet_events[0].wait_frames;
    }
}

btn::fixed_point enemy::top_left() const
{
    const btn::fixed_size& half_dimensions = _event->enemy.half_dimensions;
    return _sprite.position() - btn::fixed_point(half_dimensions.width(), half_dimensions.height());
}

bool enemy::check_hero(const btn::fixed_rect& hero_rect) const
{
    if(_life)
    {
        return btn::fixed_rect(_sprite.position(), _event->enemy.dimensions).intersects(hero_rect);
    }

    return false;
}

bool enemy::check_hero_bullet(const check_hero_bullet_data& data)
{
    if(_life && ! _ignore_hero_bullet_counter)
    {
        const enemy_data& enemy_data = _event->enemy;
        btn::fixed_point enemy_position = _sprite.position();
        btn::fixed_rect enemy_rect(enemy_position, enemy_data.dimensions);
        const btn::fixed_rect& bullet_rect = data.bullet_rect;

        if(enemy_rect.intersects(bullet_rect))
        {
            _add_damage(enemy_position, bullet_rect.x(), data.bullet_damage);

            if(! _life)
            {
                if(data.hero_ref.add_experience(enemy_data.experience))
                {
                    data.objects_ref.spawn_hero_weapon_with_sound(enemy_position, data.hero_ref.level() + 1);
                }

                switch(_event->drop)
                {

                case enemy_drop_type::NONE:
                    break;

                case enemy_drop_type::GEM:
                    data.objects_ref.spawn_gem(enemy_position);
                    break;

                case enemy_drop_type::HERO_BOMB:
                    data.objects_ref.spawn_hero_bomb_with_sound(enemy_position);
                    break;

                default:
                    BTN_ERROR("Invalid drop type: ", int(_event->drop));
                    break;
                }
            }

            return true;
        }
    }

    return false;
}

void enemy::check_hero_bomb(const btn::point& bomb_center, int bomb_squared_radius)
{
    if(_life)
    {
        btn::fixed_point enemy_position = _sprite.position();
        int distance_x = enemy_position.x().integer() - bomb_center.x();
        int distance_y = enemy_position.y().integer() - bomb_center.y();
        int squared_distance = (distance_x * distance_x) + (distance_y * distance_y);

        if(squared_distance < bomb_squared_radius)
        {
            _add_damage(enemy_position, bomb_center.x(), _life);
        }
    }
}

bool enemy::done() const
{
    return _move_event_index == _event->move_events.size();
}

void enemy::update(const btn::fixed_point& hero_position, enemy_bullets& enemy_bullets)
{
    if(_life)
    {
        if(_ignore_hero_bullet_counter)
        {
            --_ignore_hero_bullet_counter;
        }

        if(_move_event_counter)
        {
            --_move_event_counter;
        }
        else
        {
            const btn::span<const enemy_move_event>& move_events = _event->move_events;

            if(_move_event_index + 1 < move_events.size())
            {
                int previous_animation_index = move_events[_move_event_index].animation_index;
                ++_move_event_index;

                const enemy_move_event& move_event = move_events[_move_event_index];
                _move_action = btn::sprite_move_by_action(_sprite, move_event.delta_position);
                _move_event_counter = move_event.duration_frames;
                _sprite.set_horizontal_flip(move_event.horizontal_flip);

                if(move_event.animation_index != previous_animation_index)
                {
                    _animate_action = _create_animate_action(_sprite, _event->enemy, move_event.animation_index);
                }
            }
            else
            {
                if(_is_outside())
                {
                    _move_event_index = int8_t(_event->move_events.size());
                    return;
                }
            }
        }

        _move_action.update();
        _animate_action.update();

        if(_bullet_event_counter)
        {
            --_bullet_event_counter;

            if(! _bullet_event_counter)
            {
                const btn::span<const enemy_bullet_event>& bullet_events = _event->bullet_events;
                enemy_bullets.add_bullet(hero_position, _sprite.position(), bullet_events[_bullet_event_index]);
                ++_bullet_event_index;

                if(_bullet_event_index < bullet_events.size())
                {
                    _bullet_event_counter = bullet_events[_bullet_event_index].wait_frames;
                }
            }
        }
    }
    else
    {
        --_move_event_counter;

        if(_move_event_counter)
        {
            _move_action.update();

            if(_is_outside())
            {
                _move_event_counter = 1;
            }
            else
            {
                if(_rotate_action)
                {
                    _rotate_action->update();
                }

                if(_scale_x_action)
                {
                    _scale_x_action->update();
                }

                if(_scale_y_action)
                {
                    _scale_y_action->update();
                }

                if(_explosion)
                {
                    _explosion->update();

                    if(_explosion->done())
                    {
                        _explosion.reset();
                    }
                    else
                    {
                        _sprite.set_visible(_explosion->show_target_sprite());
                    }
                }
            }
        }
        else
        {
            _move_event_index = int8_t(_event->move_events.size());
        }
    }

    if(_damage_palette_counter)
    {
        --_damage_palette_counter;

        if(_damage_palette_counter == damage_frames / 2)
        {
            _sprite.set_palette(_sprite_palette);
        }
    }
}

void enemy::_add_damage(const btn::fixed_point& enemy_position, btn::fixed attack_x, int damage)
{
    int life = btn::max(_life - damage, 0);
    _life = int16_t(life);

    if(! _damage_palette_counter)
    {
        _damage_palette_counter = damage_frames;
        _sprite.set_palette(_damage_palette);
    }

    if(! life)
    {
        _event->enemy.death_sound_item.play_with_priority(constants::enemies_sound_priority);
        _move_event_counter = 1;

        switch(_event->enemy.death_anim)
        {

        case enemy_data::death_anim_type::ROTATE:
            if(btn::sprite_affine_mats::available_count() > constants::reserved_sprite_affine_mats)
            {
                _move_event_counter = 30;
                _move_action = btn::sprite_move_by_action(_sprite, 0, constants::background_speed);
                _move_action.update();

                btn::fixed rotation_angle = attack_x < enemy_position.x() ? -1 : 1;
                _rotate_action.emplace(_sprite, rotation_angle);
                _rotate_action->update();
                _scale_x_action.emplace(_sprite, _move_event_counter + 1, 0.1);
                _scale_x_action->update();
                _scale_y_action.emplace(_sprite, _move_event_counter + 1, 0.1);
                _scale_y_action->update();
            }
            break;

        case enemy_data::death_anim_type::HORIZONTAL_SCALE:
            if(btn::sprite_affine_mats::available_count() > constants::reserved_sprite_affine_mats)
            {
                _move_event_counter = 30;
                _move_action = btn::sprite_move_by_action(_sprite, 0, constants::background_speed);
                _move_action.update();
                _scale_x_action.emplace(_sprite, _move_event_counter + 1, 0.1);
                _scale_x_action->update();
            }
            break;

        case enemy_data::death_anim_type::VERTICAL_SCALE:
            if(btn::sprite_affine_mats::available_count() > constants::reserved_sprite_affine_mats)
            {
                _move_event_counter = 30;
                _move_action = btn::sprite_move_by_action(_sprite, 0, constants::background_speed);
                _move_action.update();
                _scale_y_action.emplace(_sprite, _move_event_counter + 1, 0.1);
                _scale_y_action->update();
            }
            break;

        case enemy_data::death_anim_type::EXPLOSION:
            _move_event_counter = 8 * 6;
            _move_action = btn::sprite_move_by_action(_sprite, 0, constants::background_speed / 4);
            _move_action.update();
            _explosion.emplace(btn::sprite_items::enemy_explosion, _sprite.position(), 6,
                               constants::enemy_explosions_z_order, false);
            _explosion->update();
            break;

        default:
            BTN_ERROR("Invalid death anim type: ", int(_event->enemy.death_anim));
            break;
        }
    }
}

bool enemy::_is_outside() const
{
    const btn::fixed_point& position = _sprite.position();

    return position.x() < -constants::view_width || position.x() > constants::view_width ||
            position.y() < -constants::view_height || position.y() > constants::view_height;
}

}
