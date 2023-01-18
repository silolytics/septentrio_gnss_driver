// *****************************************************************************
//
// © Copyright 2020, Septentrio NV/SA.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//    1. Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//    2. Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//    3. Neither the name of the copyright holder nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// *****************************************************************************

// *****************************************************************************
//
// Boost Software License - Version 1.0 - August 17th, 2003
//
// Permission is hereby granted, free of charge, to any person or organization
// obtaining a copy of the software and accompanying documentation covered by
// this license (the "Software") to use, reproduce, display, distribute,
// execute, and transmit the Software, and to prepare derivative works of the
// Software, and to permit third-parties to whom the Software is furnished to
// do so, all subject to the following:

// The copyright notices in the Software and this entire statement, including
// the above license grant, this restriction and the following disclaimer,
// must be included in all copies of the Software, in whole or in part, and
// all derivative works of the Software, unless such copies or derivative
// works are solely in the form of machine-executable object code generated by
// a source language processor.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
// SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
// FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//
// *****************************************************************************

#pragma once

// C++ libraries
#include <cassert> // for assert
#include <cstddef>
#include <map>
#include <sstream>
// Boost includes
#include <boost/call_traits.hpp>
#include <boost/format.hpp>
#include <boost/math/constants/constants.hpp>
#include <boost/tokenizer.hpp>
// ROSaic includes
#include <septentrio_gnss_driver/abstraction/typedefs.hpp>
#include <septentrio_gnss_driver/communication/telegram.hpp>
#include <septentrio_gnss_driver/crc/crc.hpp>
#include <septentrio_gnss_driver/parsers/nmea_parsers/gpgga.hpp>
#include <septentrio_gnss_driver/parsers/nmea_parsers/gpgsa.hpp>
#include <septentrio_gnss_driver/parsers/nmea_parsers/gpgsv.hpp>
#include <septentrio_gnss_driver/parsers/nmea_parsers/gprmc.hpp>
#include <septentrio_gnss_driver/parsers/string_utilities.hpp>

/**
 * @file message_parser.hpp
 * @brief Defines a class that reads messages handed over from the circular buffer
 */

//! Enum for NavSatFix's status.status field, which is obtained from PVTGeodetic's
//! Mode field
enum TypeOfPVT_Enum
{
    evNoPVT,
    evStandAlone,
    evDGPS,
    evFixed,
    evRTKFixed,
    evRTKFloat,
    evSBAS,
    evMovingBaseRTKFixed,
    evMovingBaseRTKFloat,
    evPPP
};

enum SbfId
{
    PVT_CARTESIAN = 4006,
    PVT_GEODETIC = 4007,
    BASE_VECTOR_CART = 4043,
    BASE_VECTOR_GEOD = 4028,
    POS_COV_CARTESIAN = 5905,
    POS_COV_GEODETIC = 5906,
    ATT_EULER = 5938,
    ATT_COV_EULER = 5939,
    CHANNEL_STATUS = 4013,
    MEAS_EPOCH = 4027,
    DOP = 4001,
    VEL_COV_GEODETIC = 5908,
    RECEIVER_STATUS = 4014,
    QUALITY_IND = 4082,
    RECEIVER_SETUP = 5902,
    INS_NAV_CART = 4225,
    INS_NAV_GEOD = 4226,
    EXT_EVENT_INS_NAV_GEOD = 4230,
    EXT_EVENT_INS_NAV_CART = 4229,
    IMU_SETUP = 4224,
    VEL_SENSOR_SETUP = 4244,
    EXT_SENSOR_MEAS = 4050,
    RECEIVER_TIME = 5914
};

namespace io {

    /**
     * @class MessageHandler
     * @brief Can search buffer for messages, read/parse them, and so on
     */
    class MessageHandler
    {
    public:
        /**
         * @brief Constructor of the MessageHandler class
         *
         * One can always provide a non-const value where a const one was expected.
         * The const-ness of the argument just means the function promises not to
         * change it.. Recall: static_cast by the way can remove or add const-ness,
         * no other C++ cast is capable of removing it (not even reinterpret_cast)
         * @param[in] node Pointer to the node)
         */
        MessageHandler(ROSaicNodeBase* node) :
            node_(node), settings_(node->settings()), unix_time_(0)
        {
        }

        /**
         * @brief Parse SBF block
         * @param[in] telegram Telegram to be parsed
         */
        void parseSbf(const std::shared_ptr<Telegram>& telegram);

        /**
         * @brief Parse NMEA block
         * @param[in] telegram Telegram to be parsed
         */
        void parseNmea(const std::shared_ptr<Telegram>& telegram);

    private:
        /**
         * @brief Header assembling
         * @param[in] frameId String of frame ID
         * @param[in] telegram telegram from which the msg was assembled
         * @param[in] msg ROS msg for which the header is assembled
         */
        template <typename T>
        void assembleHeader(const std::string& frameId,
                            const std::shared_ptr<Telegram>& telegram, T& msg);
        /**
         * @brief Publishing function
         * @param[in] topic String of topic
         * @param[in] msg ROS message to be published
         */
        template <typename M>
        void publish(const std::string& topic, const M& msg);

        /**
         * @brief Publishing function
         * @param[in] msg Localization message
         */

        void publishTf(const LocalizationMsg& msg);

        /**
         * @brief Pointer to the node
         */
        ROSaicNodeBase* node_;

        /**
         * @brief Pointer to settings struct
         */
        const Settings* settings_;

        /**
         * @brief Map of NMEA messgae IDs and uint8_t
         */
        std::unordered_map<std::string, uint8_t> nmeaMap_{
            {"$GPGGA", 0}, {"$INGGA", 0}, {"$GPRMC", 1}, {"$INRMC", 1},
            {"$GPGSA", 2}, {"$INGSA", 2}, {"$GAGSV", 3}, {"$INGSV", 3}};

        /**
         * @brief Since NavSatFix etc. need PVTGeodetic, incoming PVTGeodetic blocks
         * need to be stored
         */
        PVTGeodeticMsg last_pvtgeodetic_;

        /**
         * @brief Since NavSatFix etc. need PosCovGeodetic, incoming PosCovGeodetic
         * blocks need to be stored
         */
        PosCovGeodeticMsg last_poscovgeodetic_;

        /**
         * @brief Since GPSFix etc. need AttEuler, incoming AttEuler blocks need to
         * be stored
         */
        AttEulerMsg last_atteuler_;

        /**
         * @brief Since GPSFix etc. need AttCovEuler, incoming AttCovEuler blocks
         * need to be stored
         */
        AttCovEulerMsg last_attcoveuler_;

        /**
         * @brief Since NavSatFix, GPSFix, Imu and Pose need INSNavGeod, incoming
         * INSNavGeod blocks need to be stored
         */
        INSNavGeodMsg last_insnavgeod_;

        /**
         * @brief Since LoclaizationEcef needs INSNavCart, incoming
         * INSNavCart blocks need to be stored
         */
        INSNavCartMsg last_insnavcart_;

        /**
         * @brief Since Imu needs ExtSensorMeas, incoming ExtSensorMeas blocks
         * need to be stored
         */
        ExtSensorMeasMsg last_extsensmeas_;

        /**
         * @brief Since GPSFix needs ChannelStatus, incoming ChannelStatus blocks
         * need to be stored
         */
        ChannelStatus last_channelstatus_;

        /**
         * @brief Since GPSFix needs MeasEpoch (for SNRs), incoming MeasEpoch blocks
         * need to be stored
         */
        MeasEpochMsg last_measepoch_;

        /**
         * @brief Since GPSFix needs DOP, incoming DOP blocks need to be stored
         */
        Dop last_dop_;

        /**
         * @brief Since GPSFix needs VelCovGeodetic, incoming VelCovGeodetic blocks
         * need to be stored
         */
        VelCovGeodeticMsg last_velcovgeodetic_;

        /**
         * @brief Since DiagnosticArray needs ReceiverStatus, incoming ReceiverStatus
         * blocks need to be stored
         */
        ReceiverStatus last_receiverstatus_;

        /**
         * @brief Since DiagnosticArray needs QualityInd, incoming QualityInd blocks
         * need to be stored
         */
        QualityInd last_qualityind_;

        /**
         * @brief Since DiagnosticArray needs ReceiverSetup, incoming ReceiverSetup
         * blocks need to be stored
         */
        ReceiverSetup last_receiversetup_;

        //! When reading from an SBF file, the ROS publishing frequency is governed
        //! by the time stamps found in the SBF blocks therein.
        Timestamp unix_time_;

        //! Current leap seconds as received, do not use value is -128
        int8_t current_leap_seconds_ = -128;

        /**
         * @brief "Callback" function when constructing NavSatFix messages
         */
        void assembleNavSatFix();

        /**
         * @brief "Callback" function when constructing GPSFix messages
         */
        void assembleGpsFix();

        /**
         * @brief "Callback" function when constructing PoseWithCovarianceStamped
         * messages
         */
        void assemblePoseWithCovarianceStamped();

        /**
         * @brief "Callback" function when constructing
         * DiagnosticArrayMsg messages
         * @param[in] time_obj time of message
         */
        void assembleDiagnosticArray(const Timestamp& time_obj);

        /**
         * @brief "Callback" function when constructing
         * ImuMsg messages
         * @return A ROS message
         * ImuMsg just created
         */
        ImuMsg assmembleImu();

        /**
         * @brief "Callback" function when constructing
         * LocalizationMsg messages in UTM
         */
        void assembleLocalizationUtm();

        /**
         * @brief "Callback" function when constructing
         * LocalizationMsg messages in ECEF
         */
        void assembleLocalizationEcef();

        /**
         * @brief function to fill twist part of LocalizationMsg
         * @param[in] roll roll [rad]
         * @param[in] pitch pitch [rad]
         * @param[in] yaw yaw [rad]
         * @param[inout] msg LocalizationMsg to be filled
         */
        void assembleLocalizationMsgTwist(double roll, double pitch, double yaw,
                                          LocalizationMsg& msg);

        /**
         * @brief "Callback" function when constructing
         * TwistWithCovarianceStampedMsg messages
         * @param[in] fromIns Wether to contruct message from INS data
         */
        void assembleTwist(bool fromIns = false);

        /**
         * @brief Waits according to time when reading from file
         * @param[in] time_obj wait until time
         */
        void wait(Timestamp time_obj);

        /**
         * @brief Fixed UTM zone
         */
        std::shared_ptr<std::string> fixedUtmZone_;

        /**
         * @brief Calculates the timestamp, in the Unix Epoch time format
         * This is either done using the TOW as transmitted with the SBF block (if
         * "use_gnss" is true), or using the current time.
         * @param[in] data Pointer to the buffer
         * @return Timestamp object containing seconds and nanoseconds since last
         * epoch
         */
        Timestamp timestampSBF(const std::vector<uint8_t>& message);

        /**
         * @brief Calculates the timestamp, in the Unix Epoch time format
         * This is either done using the TOW as transmitted with the SBF block (if
         * "use_gnss" is true), or using the current time.
         * @param[in] tow (Time of Week) Number of milliseconds that elapsed since
         * the beginning of the current GPS week as transmitted by the SBF block
         * @param[in] wnc (Week Number Counter) counts the number of complete weeks
         * elapsed since January 6, 1980
         * @return Timestamp object containing seconds and nanoseconds since last
         * epoch
         */
        Timestamp timestampSBF(uint32_t tow, uint16_t wnc);
    };
} // namespace io