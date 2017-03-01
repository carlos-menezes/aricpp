/*******************************************************************************
 * ARICPP - ARI interface for C++
 * Copyright (C) 2017 Daniele Pallastrelli
 *
 * This file is part of aricpp.
 * For more information, see http://github.com/daniele77/aricpp
 *
 * Boost Software License - Version 1.0 - August 17th, 2003
 *
 * Permission is hereby granted, free of charge, to any person or organization
 * obtaining a copy of the software and accompanying documentation covered by
 * this license (the "Software") to use, reproduce, display, distribute,
 * execute, and transmit the Software, and to prepare derivative works of the
 * Software, and to permit third-parties to whom the Software is furnished to
 * do so, all subject to the following:
 *
 * The copyright notices in the Software and this entire statement, including
 * the above license grant, this restriction and the following disclaimer,
 * must be included in all copies of the Software, in whole or in part, and
 * all derivative works of the Software, unless such copies or derivative
 * works are solely in the form of machine-executable object code generated by
 * a source language processor.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
 * SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
 * FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 ******************************************************************************/


#ifndef ARICPP_CHANNEL_H_
#define ARICPP_CHANNEL_H_

#include <string>
#include "client.h"
#include "proxy.h"

namespace aricpp
{

class Channel
{
public:

    enum class State
    {
        down,
        reserved,
        offhook,
        dialing,
        ring,
        ringing,
        up,
        busy,
        dialingoffhook,
        prering,
        mute,
        unknown
    };

    Channel(const Channel& rhs) = delete;
    Channel& operator=(const Channel& rhs) = delete;
    Channel(Channel&& rhs) = default;
    Channel& operator=(Channel&& rhs) = default;
    ~Channel() = default;

    Channel(Client& _client, const std::string _id, const std::string& _state = {}) :
        id(_id), client(&_client)
    {
        StateChanged(_state);
    }

    Proxy& Ring()
    {
        return Proxy::Command("POST", "/ari/channels/"+id+"/ring", client);
    }

    Proxy& Answer()
    {
        return Proxy::Command("POST", "/ari/channels/"+id+"/answer", client);
    }

    Proxy& Hangup()
    {
        return Proxy::Command("DELETE", "/ari/channels/"+id, client);
    }

    Proxy& Call(const std::string& endpoint, const std::string& application, const std::string& callerId)
    {
        return Proxy::Command(
            "POST",
            "/ari/channels?"
            "endpoint=" + endpoint +
            "&app=" + application +
            "&channelId=" + id +
            "&callerId=" + callerId +
            "&timeout=-1"
            "&appArgs=internal",
            client
        );
    }

    const std::string& Id() const { return id; }

    bool IsDead() const { return dead; }

    State GetState() const { return state; }

    const std::string& Name() const { return name; }
    const std::string& Extension() const { return extension; }
    const std::string& CallerNum() const { return callerNum; }
    const std::string& CallerName() const { return callerName; }

private:

    friend class ChannelSet;
    void StasisStart( const std::string& _name, const std::string& _ext,
                      const std::string& _callerNum, const std::string& _callerName)
    {
        name = _name;
        extension = _ext;
        callerNum = _callerNum;
        callerName = _callerName;
    }
    void StateChanged(const std::string& s)
    {
        if ( s == "Down" ) state = State::down;
        else if ( s == "Rsrvd" ) state = State::reserved;
        else if ( s == "OffHook" ) state = State::offhook;
        else if ( s == "Dialing" ) state = State::dialing;
        else if ( s == "Ring" ) state = State::ring;
        else if ( s == "Ringing" ) state = State::ringing;
        else if ( s == "Up" ) state = State::up;
        else if ( s == "Busy" ) state = State::busy;
        else if ( s == "Dialing Offhook" ) state = State::dialingoffhook;
        else if ( s == "Pre-ring" ) state = State::prering;
        else if ( s == "Mute" ) state = State::mute;
        else if ( s == "Unknown" ) state = State::unknown;
        else state = State::unknown;
    }
    void Dead() { dead = true; }

    const std::string id;
    Client* client;
    bool dead = false;
    State state = State::unknown;
    std::string name;
    std::string extension;
    std::string callerNum;
    std::string callerName;
};

} // namespace

#endif