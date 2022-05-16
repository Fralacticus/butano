/*
 * Copyright (c) 2020-2022 Gustavo Valiente gustavo.valiente@protonmail.com
 * zlib License, see LICENSE file.
 */

#include "bn_audio_manager.h"

#include "bn_vector.h"
#include "bn_optional.h"
#include "bn_config_audio.h"
#include "bn_dmg_music_position.h"
#include "../hw/include/bn_hw_audio.h"

#include "bn_audio.cpp.h"
#include "bn_music.cpp.h"
#include "bn_sound.cpp.h"
#include "bn_dmg_music.cpp.h"
#include "bn_music_item.cpp.h"
#include "bn_sound_item.cpp.h"
#include "bn_dmg_music_item.cpp.h"

namespace bn::audio_manager
{

namespace
{
    static_assert(BN_CFG_AUDIO_MAX_COMMANDS > 2, "Invalid max audio commands");


    class command
    {

    public:
        [[nodiscard]] static command music_play(music_item item, bool loop, int volume)
        {
            return command(MUSIC_PLAY, item.id(), 0, volume, loop);
        }

        [[nodiscard]] static command music_stop()
        {
            return command(MUSIC_STOP);
        }

        [[nodiscard]] static command music_pause()
        {
            return command(MUSIC_PAUSE);
        }

        [[nodiscard]] static command music_resume()
        {
            return command(MUSIC_RESUME);
        }

        [[nodiscard]] static command music_set_position(int position)
        {
            return command(MUSIC_SET_POSITION, position);
        }

        [[nodiscard]] static command music_set_volume(int volume)
        {
            return command(MUSIC_SET_VOLUME, 0, 0, volume);
        }

        [[nodiscard]] static command dmg_music_play(dmg_music_item item, bool loop, int speed)
        {
            return command(DMG_MUSIC_PLAY, int(item.data_ptr()), 0, speed, loop);
        }

        [[nodiscard]] static command dmg_music_stop()
        {
            return command(DMG_MUSIC_STOP);
        }

        [[nodiscard]] static command dmg_music_pause()
        {
            return command(DMG_MUSIC_PAUSE);
        }

        [[nodiscard]] static command dmg_music_resume()
        {
            return command(DMG_MUSIC_RESUME);
        }

        [[nodiscard]] static command dmg_music_set_position(const bn::dmg_music_position& position)
        {
            return command(DMG_MUSIC_SET_POSITION, position.pattern(), 0, position.row());
        }

        [[nodiscard]] static command dmg_music_set_volume(int left_volume, int right_volume)
        {
            return command(DMG_MUSIC_SET_VOLUME, 0, 0, left_volume, false, right_volume);
        }

        [[nodiscard]] static command sound_play(int priority, sound_item item)
        {
            return command(SOUND_PLAY, item.id(), int16_t(priority));
        }

        [[nodiscard]] static command sound_play(int priority, sound_item item, int volume, int speed, int panning)
        {
            return command(SOUND_PLAY_EX, item.id(), int16_t(priority), volume, false, speed, panning);
        }

        [[nodiscard]] static command sound_stop_all()
        {
            return command(SOUND_STOP_ALL);
        }

        void execute() const
        {
            switch(type(_type))
            {

            case MUSIC_PLAY:
                hw::audio::play_music(_id, _volume, _loop);
                return;

            case MUSIC_STOP:
                hw::audio::stop_music();
                return;

            case MUSIC_PAUSE:
                hw::audio::pause_music();
                return;

            case MUSIC_RESUME:
                hw::audio::resume_music();
                return;

            case MUSIC_SET_POSITION:
                hw::audio::set_music_position(_id);
                return;

            case MUSIC_SET_VOLUME:
                hw::audio::set_music_volume(_volume);
                return;

            case DMG_MUSIC_PLAY:
                hw::audio::play_dmg_music(reinterpret_cast<const void*>(_id), _volume, _loop);
                return;

            case DMG_MUSIC_STOP:
                hw::audio::stop_dmg_music();
                return;

            case DMG_MUSIC_PAUSE:
                hw::audio::pause_dmg_music();
                return;

            case DMG_MUSIC_RESUME:
                hw::audio::resume_dmg_music();
                return;

            case DMG_MUSIC_SET_POSITION:
                hw::audio::set_dmg_music_position(_id, _volume);
                return;

            case DMG_MUSIC_SET_VOLUME:
                hw::audio::set_dmg_music_volume(_volume, _speed);
                return;

            case SOUND_PLAY:
                hw::audio::play_sound(_priority, _id);
                return;

            case SOUND_PLAY_EX:
                hw::audio::play_sound(_priority, _id, _volume, _speed, _panning);
                return;

            case SOUND_STOP_ALL:
                hw::audio::stop_all_sounds();
                return;

            default:
                BN_ERROR("Invalid type: ", int(_type));
                return;
            }
        }

    private:
        int _id;
        int16_t _priority;
        uint16_t _volume;
        uint16_t _speed;
        uint8_t _type;
        uint8_t _panning;
        bool _loop;

        enum type
        {
            MUSIC_PLAY,
            MUSIC_STOP,
            MUSIC_PAUSE,
            MUSIC_RESUME,
            MUSIC_SET_POSITION,
            MUSIC_SET_VOLUME,
            DMG_MUSIC_PLAY,
            DMG_MUSIC_STOP,
            DMG_MUSIC_PAUSE,
            DMG_MUSIC_RESUME,
            DMG_MUSIC_SET_POSITION,
            DMG_MUSIC_SET_VOLUME,
            SOUND_PLAY,
            SOUND_PLAY_EX,
            SOUND_STOP_ALL
        };

        explicit command(type command_type, int id = 0, int16_t priority = 0, int volume = 0, bool loop = false,
                         int speed = 0, int panning = 0) :
            _id(id),
            _priority(priority),
            _volume(uint16_t(volume)),
            _speed(uint16_t(speed)),
            _type(command_type),
            _panning(uint8_t(panning)),
            _loop(loop)
        {
        }
    };


    class static_data
    {

    public:
        vector<command, BN_CFG_AUDIO_MAX_COMMANDS> commands;
        fixed music_volume;
        bn::dmg_music_position dmg_music_position;
        fixed dmg_music_left_volume;
        fixed dmg_music_right_volume;
        int music_item_id = 0;
        int music_position = 0;
        const uint8_t* dmg_music_data = nullptr;
        bool music_playing = false;
        bool music_paused = false;
        bool dmg_music_paused = false;
    };

    BN_DATA_EWRAM static_data data;


    int _hw_music_volume(fixed volume)
    {
        return fixed_t<10>(volume).data();
    }

    int _hw_sound_volume(fixed volume)
    {
        return min(fixed_t<8>(volume).data(), 255);
    }

    int _hw_dmg_music_volume(fixed volume)
    {
        return fixed_t<3>(volume).data();
    }

    int _hw_sound_speed(fixed speed)
    {
        return min(fixed_t<10>(speed).data(), 65535);
    }

    int _hw_sound_panning(fixed panning)
    {
        return min(fixed_t<7>(panning + 1).data(), 255);
    }
}

void init(func_type hp_vblank_function, func_type lp_vblank_function)
{
    hw::audio::init(hp_vblank_function, lp_vblank_function);
}

void enable()
{
    hw::audio::enable();
}

void disable()
{
    hw::audio::disable();
}

bool music_playing()
{
    return data.music_playing;
}

optional<music_item> playing_music_item()
{
    optional<music_item> result;

    if(data.music_playing)
    {
        result = music_item(data.music_item_id);
    }

    return result;
}

void play_music(music_item item, fixed volume, bool loop)
{
    BN_ASSERT(! data.commands.full(), "No more audio commands available");

    data.commands.push_back(command::music_play(item, loop, _hw_music_volume(volume)));
    data.music_item_id = item.id();
    data.music_position = 0;
    data.music_volume = volume;
    data.music_playing = true;
    data.music_paused = false;
}

void stop_music()
{
    BN_ASSERT(data.music_playing, "There's no music playing");
    BN_ASSERT(! data.commands.full(), "No more audio commands available");

    data.commands.push_back(command::music_stop());
    data.music_playing = false;
    data.music_paused = false;
}

bool music_paused()
{
    return data.music_paused;
}

void pause_music()
{
    BN_ASSERT(data.music_playing, "There's no music playing");
    BN_ASSERT(! data.music_paused, "Music is already paused");
    BN_ASSERT(! data.commands.full(), "No more audio commands available");

    data.commands.push_back(command::music_pause());
    data.music_paused = true;
}

void resume_music()
{
    BN_ASSERT(data.music_paused, "Music is not paused");
    BN_ASSERT(! data.commands.full(), "No more audio commands available");

    data.commands.push_back(command::music_resume());
    data.music_paused = false;
}

int music_position()
{
    BN_ASSERT(data.music_playing, "There's no music playing");

    return data.music_position;
}

void set_music_position(int position)
{
    BN_ASSERT(data.music_playing, "There's no music playing");
    BN_ASSERT(! data.commands.full(), "No more audio commands available");

    data.commands.push_back(command::music_set_position(position));
    data.music_position = position;
}

fixed music_volume()
{
    BN_ASSERT(data.music_playing, "There's no music playing");

    return data.music_volume;
}

void set_music_volume(fixed volume)
{
    BN_ASSERT(data.music_playing, "There's no music playing");
    BN_ASSERT(! data.commands.full(), "No more audio commands available");

    data.commands.push_back(command::music_set_volume(_hw_music_volume(volume)));
    data.music_volume = volume;
}

bool dmg_music_playing()
{
    return data.dmg_music_data;
}

optional<dmg_music_item> playing_dmg_music_item()
{
    optional<dmg_music_item> result;

    if(const uint8_t* dmg_music_data = data.dmg_music_data)
    {
        result = dmg_music_item(*dmg_music_data);
    }

    return result;
}

void play_dmg_music(dmg_music_item item, int speed, bool loop)
{
    BN_ASSERT(! data.commands.full(), "No more audio commands available");

    data.commands.push_back(command::dmg_music_play(item, loop, speed));
    data.dmg_music_position = bn::dmg_music_position();
    data.dmg_music_left_volume = 1;
    data.dmg_music_right_volume = 1;
    data.dmg_music_data = item.data_ptr();
    data.dmg_music_paused = false;
}

void stop_dmg_music()
{
    BN_ASSERT(data.dmg_music_data, "There's no DMG music playing");
    BN_ASSERT(! data.commands.full(), "No more audio commands available");

    data.commands.push_back(command::dmg_music_stop());
    data.dmg_music_data = nullptr;
    data.dmg_music_paused = false;
}

bool dmg_music_paused()
{
    return data.dmg_music_paused;
}

void pause_dmg_music()
{
    BN_ASSERT(data.dmg_music_data, "There's no DMG music playing");
    BN_ASSERT(! data.dmg_music_paused, "DMG music is already paused");
    BN_ASSERT(! data.commands.full(), "No more audio commands available");

    data.commands.push_back(command::dmg_music_pause());
    data.dmg_music_paused = true;
}

void resume_dmg_music()
{
    BN_ASSERT(data.dmg_music_paused, "DMG music is not paused");
    BN_ASSERT(! data.commands.full(), "No more audio commands available");

    data.commands.push_back(command::dmg_music_resume());
    data.dmg_music_paused = false;
}

const bn::dmg_music_position& dmg_music_position()
{
    BN_ASSERT(data.dmg_music_data, "There's no DMG music playing");

    return data.dmg_music_position;
}

void set_dmg_music_position(const bn::dmg_music_position& position)
{
    BN_ASSERT(data.dmg_music_data, "There's no DMG music playing");
    BN_ASSERT(! data.commands.full(), "No more audio commands available");

    data.commands.push_back(command::dmg_music_set_position(position));
    data.dmg_music_position = position;
}

fixed dmg_music_left_volume()
{
    BN_ASSERT(data.dmg_music_data, "There's no DMG music playing");

    return data.dmg_music_left_volume;
}

fixed dmg_music_right_volume()
{
    BN_ASSERT(data.dmg_music_data, "There's no DMG music playing");

    return data.dmg_music_right_volume;
}

void set_dmg_music_left_volume(fixed left_volume)
{
    set_dmg_music_volume(left_volume, data.dmg_music_right_volume);
}

void set_dmg_music_right_volume(fixed right_volume)
{
    set_dmg_music_volume(data.dmg_music_left_volume, right_volume);
}

void set_dmg_music_volume(fixed left_volume, fixed right_volume)
{
    BN_ASSERT(data.dmg_music_data, "There's no DMG music playing");
    BN_ASSERT(! data.commands.full(), "No more audio commands available");

    data.commands.push_back(command::dmg_music_set_volume(
                                _hw_dmg_music_volume(left_volume), _hw_dmg_music_volume(right_volume)));
    data.dmg_music_left_volume = left_volume;
    data.dmg_music_right_volume = right_volume;
}

void play_sound(int priority, sound_item item)
{
    BN_ASSERT(! data.commands.full(), "No more audio commands available");

    data.commands.push_back(command::sound_play(priority, item));
}

void play_sound(int priority, sound_item item, fixed volume, fixed speed, fixed panning)
{
    BN_ASSERT(! data.commands.full(), "No more audio commands available");

    data.commands.push_back(command::sound_play(priority, item, _hw_sound_volume(volume), _hw_sound_speed(speed),
                                                _hw_sound_panning(panning)));
}

void stop_all_sounds()
{
    BN_ASSERT(! data.commands.full(), "No more audio commands available");

    data.commands.push_back(command::sound_stop_all());
}

bool update_on_vblank()
{
    return hw::audio::update_on_vblank();
}

void set_update_on_vblank(bool update_on_vblank)
{
    hw::audio::set_update_on_vblank(update_on_vblank);
}

void disable_vblank_handler()
{
    hw::audio::disable_vblank_handler();
}

void update()
{
    hw::audio::update();

    for(const command& command : data.commands)
    {
        command.execute();
    }

    data.commands.clear();

    if(data.music_playing && hw::audio::music_playing())
    {
        data.music_position = hw::audio::music_position();
    }

    if(data.dmg_music_data)
    {
        int pattern;
        int row;
        hw::audio::dmg_music_position(pattern, row);

        if(pattern >= 0)
        {
            data.dmg_music_position = bn::dmg_music_position(pattern, row);
        }
    }
}

void commit()
{
    hw::audio::commit();
}

void stop()
{
    data.commands.clear();

    if(data.music_playing)
    {
        stop_music();
    }

    stop_all_sounds();
}

}
