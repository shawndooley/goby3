// Copyright 2011-2020:
//   GobySoft, LLC (2013-)
//   Massachusetts Institute of Technology (2007-2014)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
//   Russ Webber <russ@rw.id.au>
//
//
// This file is part of the Goby Underwater Autonomy Project Binaries
// ("The Goby Binaries").
//
// The Goby Binaries are free software: you can redistribute them and/or modify
// them under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// The Goby Binaries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

#ifndef pTranslatorH
#define pTranslatorH

#include <google/protobuf/descriptor.h>

#include "goby/util/asio-compat.h"
#include <boost/asio.hpp>

#include "dccl/dynamic_protobuf_manager.h"
#include "goby/moos/goby_moos_app.h"
#include "goby/moos/moos_translator.h"

#include "pTranslator_config.pb.h"

namespace goby
{
namespace apps
{
namespace moos
{
class CpTranslator : public goby::moos::GobyMOOSApp
{
  public:
    static CpTranslator* get_instance();
    static void delete_instance();

  private:
    typedef boost::asio::basic_waitable_timer<goby::time::SystemClock> Timer;
    CpTranslator();
    ~CpTranslator();

    void loop(); // from GobyMOOSApp

    void create_on_publish(const CMOOSMsg& trigger_msg,
                           const goby::moos::protobuf::TranslatorEntry& entry);
    void create_on_multiplex_publish(const CMOOSMsg& moos_msg);

    void create_on_timer(const boost::system::error_code& error,
                         const goby::moos::protobuf::TranslatorEntry& entry, Timer* timer);

    void do_translation(const goby::moos::protobuf::TranslatorEntry& entry);
    void do_publish(std::shared_ptr<google::protobuf::Message> created_message);

  private:
    enum
    {
        ALLOWED_TIMER_SKEW_SECONDS = 1
    };

    goby::moos::MOOSTranslator translator_;

    boost::asio::io_context timer_io_context_;
    boost::asio::io_context::work work_;

    std::vector<std::shared_ptr<Timer> > timers_;

    static protobuf::pTranslatorConfig cfg_;
    static CpTranslator* inst_;
};
} // namespace moos
} // namespace apps
} // namespace goby

#endif
