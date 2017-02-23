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


#include <boost/program_options.hpp>
#include <algorithm>
#include <exception>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
#include <map>

#include "../aricpp/client.h"

/*

116 ---INVITE--> 132
116 <--TRYING--- 132

Create and dial 132
POST /ari/channels?endpoint=sip/291&app=attendant

                 132 ---INVITE--> 41
                 132 <--TRYING--- 41
                 132 <--RINGING-- 41

Ring 116
POST /ari/channels/<channel>/ring

116 <--RINGING-- 132

41 answers

                 132 <----OK----- 41
                 132 ----ACK----> 41

Answer 116
POST /ari/channels/<channel>/answer

116 <----OK----- 132
116 -----ACK---> 132

Bridge
POST /ari/bridges?type=mixing
POST /ari/bridges/<bridge>/addChannel?channel=<channel>&role=partecipant
POST /ari/bridges/<bridge>/addChannel?channel=<channel>&role=partecipant

                 132 ---INVITE--> 41
                       in-dialog
116 <--INVITE--- 132
      in-dialog
116 -----OK----> 132
116 <----ACK---- 132
                 132 <--TRYING--- 41
                 132 <----OK----- 41
                 132 ----ACK----> 41
                 132 ---INVITE--> 41
                       in-dialog

 */

#define CALL_TRACE

using namespace aricpp;
using namespace std;

enum class ChMode { dialing=1, dialed=2, both=3 };

class Call
{
public:
    Call( Client& c, const string& dialingCh, const string& dialedCh ) :
        client(&c), dialing(dialingCh), dialed(dialedCh)
    {
#ifdef CALL_TRACE
        cout << "Call dialing " << dialing << " dialed " << dialed
             << "created\n";
#endif
    }
#ifdef CALL_TRACE
    ~Call()
    {
        cout << "Call destroyed\n";
    }
#endif
    void Dump() const
    {
        cout << "Call dialing=" << dialing << " dialed=" << dialed << endl;
    }

    bool HasChannel(const string& ch, ChMode mode) const
    {
        return ( ( ( dialing == ch ) && ( static_cast<int>(mode) & static_cast<int>(ChMode::dialing) ) ) ||
                 ( ( dialed  == ch ) && ( static_cast<int>(mode) & static_cast<int>(ChMode::dialed)  ) ) );
    }

    void DialedChRinging()
    {
        client->RawCmd( "POST", "/ari/channels/" + dialing + "/ring", [](auto e,auto s,auto r,auto){
            if (e)
            {
                cerr << "Error in ring request: " << e.message() << '\n';
                return;
            }
            if ( s/100 != 2 )
            {
                cerr << "Negative response in ring request: " << s << ' ' << r << '\n';
                return;
            }
        });
    }

    void DialedChStart()
    {
        client->RawCmd(
            "POST",
            "/ari/channels/" + dialing + "/answer",
            [](auto,auto status,auto,auto)
            {
                if (status == 500) // Internal Server Error (the channel does not exist anymore)
                {
                    cerr << "Internal Server Error (the channel does not exist anymore)" << endl;
                }
            }
        );
    }

    void DialingChUp()
    {
        client->RawCmd(
            "POST",
            "/ari/bridges?type=mixing",
            [this](auto e,auto s,auto r,auto body)
            {
                if (e)
                {
                    cerr << "Error in bridge request: " << e.message() << '\n';
                    return;
                }
                if ( s/100 != 2 )
                {
                    cerr << "Negative response in bridge request: " << s << ' ' << r << '\n';
                    return;
                }

                auto tree = FromJson( body );
                const string bridge = Get< string >( tree, {"id"} );
                Bridge( bridge );
            }
        );
    }

    bool ChHangup( const string& id )
    {
        string* hung = ( id == dialing ? &dialing : &dialed );
        string* other = ( id == dialed ? &dialing : &dialed );

        hung->clear();
        if ( other->empty() && ! bridge.empty() )
            client->RawCmd( "DELETE", "/ari/bridges/" + bridge, [](auto,auto,auto,auto){});
        else if ( ! other->empty() )
            client->RawCmd( "DELETE", "/ari/channels/" + *other, [](auto,auto,auto,auto){});
        return ( hung->empty() && other->empty() );
    }

private:

    void Bridge( const string& bridgeId )
    {
#ifdef CALL_TRACE
        cout << "Call bridge\n";
#endif
        bridge = bridgeId;
        client->RawCmd(
            "POST",
            "/ari/bridges/" + bridge + "/addChannel?channel=" + dialing + "," + dialed,
            [](auto e,auto s,auto r,auto)
            {
                if (e)
                {
                    cerr << "Error in bridge request: " << e.message() << '\n';
                    return;
                }
                if ( s/100 != 2 )
                {
                    cerr << "Negative response in bridge request: " << s << ' ' << r << '\n';
                    return;
                }
            }
        );
    }

    Client* client;
    string dialing;
    string dialed;
    string bridge;
};

class CallContainer
{
public:
    explicit CallContainer( const string& app, Client& c ) :
        application( app ), connection( c )
    {
        connection.OnEvent(
            "StasisStart",
            [this](const JsonTree& e)
            {
                // Dump(e);

                const auto& args = Get< vector< string > >( e, {"args"} );
                if ( args.empty() ) DialingChannel( e );
                else DialedChannel( e );
            }
        );
        connection.OnEvent(
            "ChannelDestroyed",
            [this](const JsonTree& e)
            {
                // Dump(e);

                auto id = Get< string >( e, {"channel", "id"} );
                auto call = FindCallByChannel(id, ChMode::both);
                if (call != calls.size())
                {
                    if ( calls[call].ChHangup( id ) )
                        Remove(call);
                }
                else
                    cerr << "Call with a channel " << id << " not found (hangup event)" << endl;
            }
        );
        connection.OnEvent(
            "ChannelStateChange",
            [this](const JsonTree& e)
            {
                // Dump(e);

                auto id = Get< string >( e, {"channel", "id"} );
                auto state = Get< string >( e, {"channel", "state"} );
                if ( state == "Ringing" )
                {
                    auto call = FindCallByChannel(id, ChMode::dialed);
                    if (call == calls.size())
                        cerr << "Call with dialed ch id " << id << " not found (ringing event)\n";
                    else
                        calls[call].DialedChRinging();
                }
                else if ( state == "Up" )
                {
                    auto call = FindCallByChannel(id, ChMode::dialing);
                    if (call != calls.size()) calls[call].DialingChUp();
                }
            }
        );
    }
    CallContainer(const CallContainer&) = delete;
    CallContainer(CallContainer&&) = delete;
    CallContainer& operator=(const CallContainer&) = delete;

private:

    void DialingChannel( const JsonTree& e )
    {
        const string callingId = Get< string >( e, {"channel", "id"} );
        const string name = Get< string >( e, {"channel", "name"} );
        const string ext = Get< string >( e, {"channel", "dialplan", "exten"} );
        const string callerNum = Get< string >( e, {"channel", "caller", "number"} );
        string callerName = Get< string >( e, {"channel", "caller", "name"} );
        if (callerName.empty()) callerName = callerNum;

        // generate an id for the called
        const string dialedId = "aricpp-" + to_string( nextId++ );

        // create a new call object
        Create( callingId, dialedId );

        // call the called party
        connection.RawCmd(
            "POST",
            "/ari/channels?"
            "endpoint=sip/" + ext +
            "&app=" + application +
            "&channelId=" + dialedId +
            "&callerId=" + callerName +
            "&timeout=-1"
            "&appArgs=dialed," + callingId,
            [this,callingId](auto e,auto s,auto r,auto)
            {
                if (e) cerr << "Error creating channel: " << e.message() << '\n';
                if (s/100 != 2)
                {
                    cerr << "Error: status code " << s << " reason: " << r << '\n';
                    connection.RawCmd( "DELETE", "/ari/channels/"+callingId, [](auto,auto,auto,auto){});
                }
             }
        );
    }

    void DialedChannel( const JsonTree& e )
    {
        auto dialed = Get< string >( e, {"channel", "id"} );
        auto call = FindCallByChannel(dialed, ChMode::dialed);
        if (call == calls.size())
            cerr << "Call with dialed ch id " << dialed << " not found (stasis start event)\n";
        else
            calls[call].DialedChStart();
    }

    // Creates a new call having given channels
    void Create(const string& dialingId, const string& dialedId)
    {
#ifdef CALL_TRACE
        cout << "create call from ch id: " << dialingId << '\n';
#endif
        calls.emplace_back(connection, dialingId, dialedId);
    }

    void Remove(size_t callIndex)
    {
        assert(callIndex < calls.size());
        calls.erase(calls.begin()+callIndex);
    }

    // return the index of the call in the vector.
    // return calls.size() if not found
    size_t FindCallByChannel(const string& ch, ChMode mode) const
    {
        for(size_t i=0; i<calls.size(); ++i)
            if (calls[i].HasChannel(ch, mode)) return i;
        return calls.size();
    }

    const string application;
    Client& connection;
    vector<Call> calls;
    unsigned long long nextId = 0;
};


int main( int argc, char* argv[] )
{
    try
    {
        string host = "localhost";
        string port = "8088";
        string username = "asterisk";
        string password = "asterisk";
        string application = "attendant";

        namespace po = boost::program_options;
        po::options_description desc("Allowed options");
        desc.add_options()
            ("help,h", "produce help message")
            ("version,V", "print version string")

            ("host,H", po::value(&host), ("ip address of the ARI server ["s + host + ']').c_str())
            ("port,P", po::value(&port), ("port of the ARI server ["s + port + "]").c_str())
            ("username,u", po::value(&username), ("username of the ARI account on the server ["s + username + "]").c_str())
            ("password,p", po::value(&password), ("password of the ARI account on the server ["s + password + "]").c_str())
            ("application,a", po::value(&application), ("stasis application to use ["s + application + "]").c_str())
        ;

        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);

        if (vm.count("help"))
        {
            cout << desc << "\n";
            return 0;
        }

        if (vm.count("version"))
        {
            cout << "This is dial application v. 1.0, part of aricpp library\n";
            return 0;
        }

        boost::asio::io_service ios;

        // Register to handle the signals that indicate when the server should exit.
        // It is safe to register for the same signal multiple times in a program,
        // provided all registration for the specified signal is made through Asio.
        boost::asio::signal_set signals(ios);
        signals.add(SIGINT);
        signals.add(SIGTERM);
#if defined(SIGQUIT)
        signals.add(SIGQUIT);
#endif // defined(SIGQUIT)
        signals.async_wait(
            [&ios](boost::system::error_code /*ec*/, int /*signo*/)
            {
                cout << "Cleanup and exit application...\n";
                ios.stop();
            });

        Client client( ios, host, port, username, password, application );
        CallContainer calls( application, client );

        client.Connect( [&](boost::system::error_code e){
            if (e)
            {
                cerr << "Connection error: " << e.message() << endl;
                ios.stop();
            }
            else
                cout << "Connected" << endl;
        });

        ios.run();
    }
    catch ( exception& e )
    {
        cerr << "Exception in app: " << e.what() << ". Aborting\n";
        return -1;
    }
    return 0;
}
