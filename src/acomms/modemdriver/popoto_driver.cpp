// Copyright 2020:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Thomas McCabe <tom.mccabe@missionsystems.com.au>
//   Toby Schneider <toby@gobysoft.org>
//
//
// This file is part of the Goby Underwater Autonomy Project Libraries
// ("The Goby Libraries").
//
// The Goby Libraries are free software: you can redistribute them and/or modify
// them under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 2.1 of the License, or
// (at your option) any later version.
//
// The Goby Libraries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

/************************************************************/
/*    NAME: Thomas McCabe                                   */
/*    ORGN: Mission Systems Pty Ltd                         */
/*    FILE: Popoto.cpp                                      */
/*    DATE: Aug 20 2020                                    */
/************************************************************/

/* Copyright (c) 2020 mission systems pty ltd */

#include "popoto_driver.h"
#include <iostream>

#include "driver_exception.h"
#include "goby/util/binary.h"
#include "goby/util/debug_logger.h"
#include "goby/util/protobuf/io.h"

using goby::glog;
using namespace goby::util::logger;
using json = nlohmann::json;

goby::acomms::PopotoDriver::PopotoDriver() {}
goby::acomms::PopotoDriver::~PopotoDriver() {}

void goby::acomms::PopotoDriver::startup(const protobuf::DriverConfig& cfg)
{
    driver_cfg_ = cfg;

    // Popoto specific start up strings
    int modem_p = popoto_driver_cfg().modem_power();
    int payload_mode = popoto_driver_cfg().payload_mode();
    int start_timeout = popoto_driver_cfg().start_timeout();

    // Set the default baud
    if (!driver_cfg_.has_serial_baud())
        driver_cfg_.set_serial_baud(DEFAULT_BAUD);

    glog.is(DEBUG1) && glog << group(glog_out_group()) << "PopotoDriver: Starting modem..."
                            << std::endl;
    ModemDriverBase::modem_start(driver_cfg_);

    // -------------------------- set the Popoto CFG params in the right format ------------
    std::stringstream raw;
    raw << "setvaluef TxPowerWatts " + std::to_string(modem_p) << "\n";
    signal_and_write(raw.str());

    raw.str(""); // clear the string stream
    raw << "setvaluei PayloadMode " + std::to_string(payload_mode) << "\n";
    signal_and_write(raw.str());

    raw.str("");
    raw << "setvaluei LedEnable " + std::to_string(0) << "\n";
    signal_and_write(raw.str());

    raw.str("");
    raw << "setvaluei LocalID " << driver_cfg_.modem_id() << "\n";
    signal_and_write(raw.str());

    // Poll the modem temp and battery voltage
    signal_and_write("getvaluef BatteryVoltage\n");
    signal_and_write("getvaluef Temp_Ambient\n");

    // Check if modem has started
    std::string in;
    int startup_elapsed_ms = 0;

    while (!modem_read(&in))
    {
        usleep(100000); // 100 ms
        startup_elapsed_ms += 100;

        if (startup_elapsed_ms / 1000 >= start_timeout)
            throw(ModemDriverException("Modem physical connection failed to startup.",
                                       protobuf::ModemDriverStatus::STARTUP_FAILED));
    }

    glog.is(DEBUG1) && glog << "Modem " << driver_cfg_.modem_id() << " initialized OK."
                            << std::endl;
}

void goby::acomms::PopotoDriver::shutdown() { ModemDriverBase::modem_close(); }

// --------------------------- Outgoing msgs ------------------------------------------------
void goby::acomms::PopotoDriver::handle_initiate_transmission(
    const protobuf::ModemTransmission& orig_msg)
{
    protobuf::ModemTransmission msg = orig_msg;
    // Poll the modem temp and battery voltage before each transmission
    signal_and_write("getvaluef BatteryVoltage\n");
    signal_and_write("getvaluef Temp_Ambient\n");

    switch (msg.type())
    {
        case protobuf::ModemTransmission::DATA:
        {
            msg.set_max_num_frames(1);

            if (!msg.has_max_frame_bytes())
                msg.set_max_frame_bytes(DEFAULT_MTU_BYTES);

            ModemDriverBase::signal_modify_transmission(&msg);

            msg.set_frame_start(0);

            if (msg.frame_size() == 0)
                ModemDriverBase::signal_data_request(&msg);

            if (msg.frame_size() > 0 && msg.frame(0).size() > 0)
            {
                glog.is(DEBUG1) && glog << group(glog_out_group())
                                        << "We were asked to transmit from " << msg.src() << " to "
                                        << msg.dest() << " at bitrate code " << msg.rate()
                                        << std::endl;
                glog.is(DEBUG1) && glog << group(glog_out_group()) << "Sending these data now: "
                                        << goby::util::hex_encode(msg.frame(0)) << std::endl;
                send(msg);
            }
            break;
        }

        case protobuf::ModemTransmission::DRIVER_SPECIFIC:
        {
            switch (msg.GetExtension(popoto::protobuf::transmission).type())
            {
                case popoto::protobuf::POPOTO_TWO_WAY_RANGE:
                    send_range_request(msg.dest()); // send a ranging message
                    break;

                case popoto::protobuf::POPOTO_PLAY_FILE: play_file(msg); break;

                case popoto::protobuf::POPOTO_TWO_WAY_PING: send_ping(msg); break;

                case popoto::protobuf::POPOTO_DEEP_SLEEP: popoto_sleep(); break;

                case popoto::protobuf::POPOTO_WAKE:
                    send_wake(); // the wake will just be a ping for the moment
                    break;

                default:
                    glog.is(DEBUG1) &&
                        glog << group(glog_out_group()) << warn
                             << "Not initiating transmission because we were given an invalid "
                                "DRIVER_SPECIFIC transmission type for the Popoto Modem"
                             << msg << std::endl;
                    break;
            }
        }

        default:
            glog.is(WARN) && glog << group(glog_out_group()) << "Unsupported transmission type: "
                                  << protobuf::ModemTransmission::TransmissionType_Name(msg.type())
                                  << std::endl;
            break;
    }
}
//--------------------------------------- send_wake ------------------------------------------------------------
// Send a wake command to the other modem, this can be any message so using a ping which can be changed if needed
void goby::acomms::PopotoDriver::send_wake(void)
{
    std::stringstream message;
    message << "ping " << popoto_driver_cfg().modem_power() << std::endl;

    signal_and_write(message.str());
}
//--------------------------------------- popoto_sleep ---------------------------------------------------------
// Send a sleep command to the current modem
void goby::acomms::PopotoDriver::popoto_sleep(void)
{
    glog.is(DEBUG1) && glog << "Modem will now sleep: " << std::endl;
    signal_and_write(
        "powerdown\n"); //This will put the modem into deep sleep mode to wake up on next acoustic signal
}

//--------------------------------------- play_file ------------------------------------------------------------
// Play a file from the modems directory
void goby::acomms::PopotoDriver::play_file(protobuf::ModemTransmission& msg)
{
    glog.is(DEBUG1) && glog << msg.DebugString() << std::endl;

    std::stringstream message;
    message << "playstart " << msg.GetExtension(popoto::protobuf::transmission).file_location()
            << " " << msg.GetExtension(popoto::protobuf::transmission).transmit_power() << "\n";

    // send over the wire
    signal_and_write("playstop\n"); // need to make sure nothing else is playing
    signal_and_write(message.str());
}
//--------------------------------------- send_ping ------------------------------------------------------------
// Send a ping
void goby::acomms::PopotoDriver::send_ping(protobuf::ModemTransmission& msg)
{
    glog.is(DEBUG1) && glog << msg.DebugString() << std::endl;

    std::stringstream message;
    message << "ping " << msg.GetExtension(popoto::protobuf::transmission).transmit_power() << "\n";

    signal_and_write(message.str());
}

// ------------------------------------- Send -------------------------------------------------
void goby::acomms::PopotoDriver::send(protobuf::ModemTransmission& msg)
{
    // Set bitrate
    // Bitrates with Popoto modem: map these onto 0-5
    std::vector<std::string> rate_to_speed{"setRate80\n",   "setRate640\n",  "setRate1280\n",
                                           "setRate2560\n", "setRate5120\n", "setRate10240\n"};
    int min_rate = 0, max_rate = rate_to_speed.size() - 1;
    int rate = msg.rate();

    if (rate < min_rate || rate > max_rate)
    {
        glog.is(WARN) && glog << "Invalid rate, must be between " << min_rate << " and " << max_rate
                              << ". Using rate: " << min_rate << std::endl;
        rate = min_rate;
    }
    signal_and_write(rate_to_speed[rate]);

    int dest = (msg.dest() == goby::acomms::BROADCAST_ID) ? POPOTO_BROADCAST_ID : msg.dest();
    std::stringstream raw1;
    raw1 << "setvaluei RemoteID " << dest << "\n";
    signal_and_write(raw1.str());

    auto goby_header = CreateGobyHeader(msg);
    std::string jsonStr = binary_to_json(&goby_header, 1);
    if (msg.type() == protobuf::ModemTransmission::DATA)
    {
        std::vector<std::uint8_t> bytes(msg.frame(0).begin(), msg.frame(0).end());
        jsonStr += "," + binary_to_json(&bytes[0], bytes.size());
    }
    else if (msg.type() == protobuf::ModemTransmission::ACK)
    {
        // use empty data packet to indicate ACK
        // TODO - get Popoto to provide ACK packet type in header
    }
    else
    {
        throw(goby::Exception(std::string("Unsupported type provided to send: ") +
                              protobuf::ModemTransmission::TransmissionType_Name(msg.type())));
    }

    // To send a bin msg it needs to be in 8 bit CSV values
    std::stringstream raw;
    raw << "transmitJSON {\"Payload\":{\"Data\":[" << jsonStr << "]}}"
        << "\n";

    // Send the raw string to terminal for debugging
    glog.is(DEBUG1) && glog << raw.str() << std::endl;

    // Send over the wire
    signal_and_write(raw.str());
}

// ---------------------------- Ranging ----------------------------------------------------
void goby::acomms::PopotoDriver::send_range_request(int dest)
{
    std::stringstream raw;
    raw << "setvaluei RemoteID " << dest << "\n";
    signal_and_write(raw.str());

    // A range messages needs to be formated with 'range txPower' we will use the default power from .moos launch file
    std::stringstream range;
    range << "range " << popoto_driver_cfg().modem_power() << "\n";

    // Send over the wire
    signal_and_write(range.str());
}

// --------------------------- Incoming msgs ------------------------------------------------
void goby::acomms::PopotoDriver::do_work()
{
    std::string in;
    while (modem_read(&in))
    {
        try
        {
            // Remove VT100 sequences (if they exist) and popoto prompt
            in = StripString(in, "Popoto->");
            constexpr const char *VT100_BOLD_ON = "\x1b[1m", *VT100_BOLD_OFF = "\x1b[0m";

            in = StripString(in, VT100_BOLD_ON);
            in = StripString(in, VT100_BOLD_OFF);

            protobuf::ModemRaw raw;
            raw.set_raw(in);
            ModemDriverBase::signal_raw_incoming(raw);

            if (json::accept(in))
            {
                ProcessJSON(in, modem_msg_);
                if (modem_msg_.has_type())
                {
                    glog.is(DEBUG1) && glog << group(glog_in_group()) << "received: " << modem_msg_
                                            << std::endl;

                    if (modem_msg_.type() == protobuf::ModemTransmission::DATA &&
                        modem_msg_.ack_requested() && modem_msg_.dest() == driver_cfg_.modem_id())
                    {
                        // make any acks
                        protobuf::ModemTransmission ack;
                        ack.set_src(driver_cfg_.modem_id());
                        ack.set_dest(modem_msg_.src());

                        // make the acks at rate 0 for highest reliability
                        ack.set_rate(0);

                        ack.set_type(goby::acomms::protobuf::ModemTransmission::ACK);
                        ack.add_acked_frame(0);

                        send(ack); // reply with the ack msg
                    }
                    ModemDriverBase::signal_receive(modem_msg_);
                    modem_msg_.Clear();
                }
            }
        }
        catch (std::exception& e)
        {
            glog.is(WARN) && glog << "Bad line: " << in << std::endl;
            glog.is(WARN) && glog << "Exception: " << e.what() << std::endl;
        }
    }
}
// --------------------------- Write over the wire ------------------------------------------------
void goby::acomms::PopotoDriver::signal_and_write(const std::string& raw)
{
    protobuf::ModemRaw raw_msg;
    raw_msg.set_raw(raw);
    ModemDriverBase::signal_raw_outgoing(raw_msg);

    glog.is(DEBUG1) && glog << group(glog_out_group()) << boost::trim_copy(raw) << std::endl;
    ModemDriverBase::modem_write(raw);
}

// Converts the dccl binary to the required comma seperated bytes
std::string goby::acomms::PopotoDriver::binary_to_json(const std::uint8_t* buf, size_t num_bytes)
{
    std::string output;

    for (int i = 0; i < num_bytes; i++)
    {
        output.append(std::to_string((uint8_t)buf[i]));
        if (i < num_bytes - 1)
        {
            output.append(",");
        }
    }

    return output;
}

// Convert csv values back to dccl binary for the dccl codec to decode
std::string goby::acomms::PopotoDriver::json_to_binary(const json& element)
{
    std::string output;

    for (auto& subel : element) { output.append(1, (char)((uint8_t)subel)); }

    return output;
}

// Decode Popoto header
void goby::acomms::PopotoDriver::DecodeHeader(std::vector<uint8_t> data,
                                              protobuf::ModemTransmission& modem_msg)
{
    std::string type;

    // Popoto header types
    enum PopotoMessageType
    {
        DATA_MESSAGE = 0,
        RANGE_RESPONSE = 128,
        RANGE_REQUEST = 129,
        STATUS = 130
    };

    // Process binary payload data
    switch (data[0])
    {
        case DATA_MESSAGE:
            type = "Data message";
            {
                std::uint16_t payload_info{0};
                payload_info |= data[4] & 0xFF;
                payload_info |= (data[5] & 0xFF) << 8;

                // lowest 10 bits
                std::uint16_t length = payload_info & 0x3FF;
                // bit 10
                // bool streaming = payload_info & 0x400;
                // bits 11-15
                std::uint16_t modulation = (payload_info & 0xF800) >> 11;

                // use empty data packet to indicate ACK
                // TODO - get Popoto to provide ACK packet type in header
                if (length == 0)
                    modem_msg.set_type(protobuf::ModemTransmission::ACK);

                std::vector<int> modulation_to_rate{0, 4, 3, 2, 1, 5};
                if (modulation < modulation_to_rate.size())
                    modem_msg.set_rate(modulation_to_rate[modulation]);

                break;
            }

        case RANGE_RESPONSE: type = "Range response"; break;

        case RANGE_REQUEST: type = "Range_request"; break;

        case STATUS: type = "Status message"; break;

        default: glog.is(DEBUG1) && glog << "Unknown message type: " << data[0] << std::endl; break;
    }

    int sender = data[1];
    modem_msg.set_src(sender == POPOTO_BROADCAST_ID ? goby::acomms::BROADCAST_ID : sender);
    int receiver = data[2];
    modem_msg.set_dest(receiver == POPOTO_BROADCAST_ID ? goby::acomms::BROADCAST_ID : receiver);
    int tx_power = data[3];
    sender_id_ = sender;
    glog.is(DEBUG1) && glog << type << " from " << sender << " to " << receiver
                            << " at tx power: " << tx_power << std::endl;
}

// The only msg that is important for the dccl driver is the header and data. We will just print everything else to terminal for the moment
void goby::acomms::PopotoDriver::ProcessJSON(std::string message,
                                             protobuf::ModemTransmission& modem_msg)
{
    json j = json::parse(message);
    json::iterator it = j.begin();
    std::string label = it.key();
    std::string str;

    protobuf::ModemRaw raw;
    raw.set_raw(message);

    if (label == "Header")
    {
        DecodeHeader(j["Header"], modem_msg);
    }
    else if (label == "Data")
    {
        std::string data = json_to_binary(j["Data"]);
        DecodeGobyHeader(data[0], modem_msg);
        if (modem_msg.type() == protobuf::ModemTransmission::DATA)
            *modem_msg.add_frame() = data.substr(1);
        else if (modem_msg.type() == protobuf::ModemTransmission::ACK)
            modem_msg.add_acked_frame(0);
    }
    else if (label == "Alert")
    {
        glog.is(DEBUG1) && glog << "Alert: " << j["Alert"] << std::endl;
    }
    else if (label == "SNRdB")
    {
        glog.is(DEBUG1) && glog << "SNRdB: " << j["SNRdB"] << std::endl;
    }
    else if (label == "DopplerVelocity")
    {
        glog.is(DEBUG1) && glog << "DopplerVelocity" << j["DopplerVelocity"] << std::endl;
    }
    else if (label == "Info")
    {
        glog.is(DEBUG1) && glog << "Info: " << j["Info"] << std::endl;
    }
}
// Remove popoto trash from the incoming serial string
std::string goby::acomms::PopotoDriver::StripString(std::string in, std::string p)
{
    std::string out = in;
    std::string::size_type n = p.length();
    for (std::string::size_type i = out.find(p); i != std::string::npos; i = out.find(p))
        out.erase(i, n);

    return out;
}

// one byte header for information we need to send that isn't contained in the Popoto header
std::uint8_t goby::acomms::PopotoDriver::CreateGobyHeader(const protobuf::ModemTransmission& m)
{
    std::uint8_t header{0};
    if (m.type() == protobuf::ModemTransmission::DATA)
    {
        header |= 0 << GOBY_HEADER_TYPE;
        header |= (m.ack_requested() ? 1 : 0) << GOBY_HEADER_ACK_REQUEST;
    }
    else if (m.type() == protobuf::ModemTransmission::ACK)
    {
        header |= 1 << GOBY_HEADER_TYPE;
    }
    else
    {
        throw(goby::Exception(std::string("Unsupported type provided to CreateGobyHeader: ") +
                              protobuf::ModemTransmission::TransmissionType_Name(m.type())));
    }
    return header;
}

void goby::acomms::PopotoDriver::DecodeGobyHeader(std::uint8_t header,
                                                  protobuf::ModemTransmission& m)
{
    m.set_type((header & (1 << GOBY_HEADER_TYPE)) ? protobuf::ModemTransmission::ACK
                                                  : protobuf::ModemTransmission::DATA);
    if (m.type() == protobuf::ModemTransmission::DATA)
        m.set_ack_requested(header & (1 << GOBY_HEADER_ACK_REQUEST));
}
