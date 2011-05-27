// copyright 2008, 2009 t. schneider tes@mit.edu
// 
// this file is part of the Dynamic Compact Control Language (DCCL),
// the goby-acomms codec. goby-acomms is a collection of libraries 
// for acoustic underwater networking
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This software is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this software.  If not, see <http://www.gnu.org/licenses/>.

#ifndef MESSAGE_VAR_FLOAT20100317H
#define MESSAGE_VAR_FLOAT20100317H

#include "goby/util/string.h"

#include "message_var.h"

#include <boost/math/special_functions/fpclassify.hpp>

namespace goby
{
    namespace transitional
    {   
        class DCCLMessageVarFloat : public DCCLMessageVar
        {
          public:
            DCCLMessageVarFloat(double max = std::numeric_limits<double>::max(), double min = 0, double precision = 0);

        
            void set_max(double max) {max_ = max;}
            void set_max(const std::string& s) { set_max(util::as<double>(s)); }
        
            void set_min(double min) {min_ = min;}
            void set_min(const std::string& s) { set_min(util::as<double>(s)); }

            void set_precision(int precision) {precision_ = precision;}
            void set_precision(const std::string& s) { set_precision(util::as<int>(s)); }
        
            int precision() const { return precision_; }  

            double min() const { return min_; }
            double max() const { return max_; }
        
            void set_max_delta(double max_delta)
            { max_delta_ = max_delta; }
            void set_max_delta(const std::string& s)
            { set_max_delta(util::as<double>(s)); }

            virtual DCCLType type() const { return dccl_float; }


          protected:
            virtual DCCLMessageVal cast(double d, int precision) { return DCCLMessageVal(d, precision); }
            virtual void initialize_specific();
            
            virtual void pre_encode(DCCLMessageVal& v);
            
            
          private:
            bool is_delta() const
            { return using_delta_differencing() && !is_key_frame_; }
        
            bool using_delta_differencing() const
            { return !(boost::math::isnan)(max_delta_); }

        
          private:
            double max_;
            double min_;
            int precision_;

            double max_delta_;
        };

    }
}
#endif
