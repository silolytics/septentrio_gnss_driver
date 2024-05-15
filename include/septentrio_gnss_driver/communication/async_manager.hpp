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

// Boost includes
#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>
#include <boost/regex.hpp>

// ROSaic includes
#include <septentrio_gnss_driver/crc/crc.hpp>
#include <septentrio_gnss_driver/parsers/parsing_utilities.hpp>

// local includes
#include <septentrio_gnss_driver/communication/io.hpp>
#include <septentrio_gnss_driver/communication/telegram.hpp>

/**
 * @file async_manager.hpp
 * @date 20/08/20
 * @brief Implements asynchronous operations for an I/O manager
 *
 * Such operations include reading NMEA messages and SBF blocks yet also sending
 * commands to serial port or via TCP/IP.
 */

namespace io {

    /**
     * @class AsyncManagerBase
     * @brief Interface (in C++ terms), that could be used for any I/O manager,
     * synchronous and asynchronous alike
     */
    class AsyncManagerBase
    {
    public:
        virtual ~AsyncManagerBase() {}
        //! Connects the stream
        [[nodiscard]] virtual bool connect() = 0;
        //! Sends commands to the receiver
        virtual void send(const std::string& cmd) = 0;
    };

    /**
     * @class AsyncManager
     * @brief This is the central interface between ROSaic and the Rx(s), managing
     * I/O operations such as reading messages and sending commands..
     *
     * IoType is either boost::asio::serial_port or boost::asio::tcp::ip
     */
    template <typename IoType>
    class AsyncManager : public AsyncManagerBase
    {
    public:
        /**
         * @brief Class constructor
         * @param[in] node Pointer to node
         * @param[in] telegramQueue Telegram queue
         */
        AsyncManager(ROSaicNodeBase* node, TelegramQueue* telegramQueue);

        ~AsyncManager();

        [[nodiscard]] bool connect();

        void setPort(const std::string& port);

        void send(const std::string& cmd);

    private:
        void receive();
        void close();
        void runIoService();
        void runWatchdog();
        void write(const std::string& cmd);
        void resync();
        template <uint8_t index>
        void readSync();
        void readSbfHeader();
        void readSbf(std::size_t length);
        void readUnknown();
        void readString();
        void readStringElements();

        //! Pointer to the node
        ROSaicNodeBase* node_;
        std::shared_ptr<boost::asio::io_service> ioService_;
        IoType ioInterface_;
        std::atomic<bool> running_;
        std::thread ioThread_;
        std::thread watchdogThread_;

        std::array<uint8_t, 1> buf_;
        //! Timestamp of receiving buffer
        Timestamp recvStamp_;
        //! Telegram
        std::shared_ptr<Telegram> telegram_;
        //! TelegramQueue
        TelegramQueue* telegramQueue_;
    };

    template <typename IoType>
    AsyncManager<IoType>::AsyncManager(ROSaicNodeBase* node,
                                       TelegramQueue* telegramQueue) :
        node_(node), ioService_(new boost::asio::io_service),
        ioInterface_(node, ioService_), telegramQueue_(telegramQueue)
    {
        node_->log(log_level::DEBUG, "AsyncManager created.");
    }

    template <typename IoType>
    AsyncManager<IoType>::~AsyncManager()
    {
        running_ = false;
        close();
        node_->log(log_level::DEBUG, "AsyncManager shutting down threads");
        ioService_->stop();
        ioThread_.join();
        watchdogThread_.join();
        node_->log(log_level::DEBUG, "AsyncManager threads stopped");
    }

    template <typename IoType>
    [[nodiscard]] bool AsyncManager<IoType>::connect()
    {
        running_ = true;

        if (!ioInterface_.connect())
        {
            return false;
        }
        receive();

        return true;
    }

    template <typename IoType>
    void AsyncManager<IoType>::setPort(const std::string& port)
    {
        ioInterface_.setPort(port);
    }

    template <typename IoType>
    void AsyncManager<IoType>::send(const std::string& cmd)
    {
        if (cmd.size() == 0)
        {
            node_->log(log_level::ERROR,
                       "AsyncManager message size to be sent to the Rx would be 0");
            return;
        }

        ioService_->post(boost::bind(&AsyncManager<IoType>::write, this, cmd));
    }

    template <typename IoType>
    void AsyncManager<IoType>::receive()
    {
        resync();
        ioThread_ =
            std::thread(std::bind(&AsyncManager<IoType>::runIoService, this));
        if (!watchdogThread_.joinable())
            watchdogThread_ =
                std::thread(std::bind(&AsyncManager::runWatchdog, this));
    }

    template <typename IoType>
    void AsyncManager<IoType>::close()
    {
        ioService_->post([this]() { ioInterface_.close(); });
    }

    template <typename IoType>
    void AsyncManager<IoType>::runIoService()
    {
        ioService_->run();
        node_->log(log_level::DEBUG, "AsyncManager ioService terminated.");
    }

    template <typename IoType>
    void AsyncManager<IoType>::runWatchdog()
    {
        while (running_)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            if (running_ && ioService_->stopped())
            {
                if (node_->settings()->read_from_sbf_log ||
                    node_->settings()->read_from_pcap)
                {
                    node_->log(
                        log_level::INFO,
                        "AsyncManager finished reading file. Node will continue to publish queued messages.");
                    break;
                } else
                {
                    node_->log(log_level::ERROR,
                               "AsyncManager connection lost. Trying to reconnect.");
                    ioService_->reset();
                    ioThread_.join();
                    while (!ioInterface_.connect())
                        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                    receive();
                }
            } else if (running_ && std::is_same<TcpIo, IoType>::value)
            {
                node_->log(log_level::DEBUG, "ping.");
                // Send to check if TCP connection still alive
                std::string empty = " ";
                boost::asio::async_write(
                    *(ioInterface_.stream_), boost::asio::buffer(empty.data(), 1),
                    [](boost::system::error_code ec, std::size_t /*length*/) {});
            }
        }
    }

    template <typename IoType>
    void AsyncManager<IoType>::write(const std::string& cmd)
    {
        boost::asio::async_write(
            *(ioInterface_.stream_), boost::asio::buffer(cmd.data(), cmd.size()),
            [this, cmd](boost::system::error_code ec, std::size_t /*length*/) {
                if (!ec)
                {
                    // Prints the data that was sent
                    node_->log(log_level::DEBUG, "AsyncManager sent the following " +
                                                     std::to_string(cmd.size()) +
                                                     " bytes to the Rx: " + cmd);
                } else
                {
                    node_->log(log_level::ERROR,
                               "AsyncManager was unable to send the following " +
                                   std::to_string(cmd.size()) +
                                   " bytes to the Rx: " + cmd);
                }
            });
    }

    template <typename IoType>
    void AsyncManager<IoType>::resync()
    {
        telegram_.reset(new Telegram);
        readSync<0>();
    }

    template <typename IoType>
    template <uint8_t index>
    void AsyncManager<IoType>::readSync()
    {
        static_assert(index < 3);

        boost::asio::async_read(
            *(ioInterface_.stream_),
            boost::asio::buffer(telegram_->message.data() + index, 1),
            [this](boost::system::error_code ec, std::size_t numBytes) {
                Timestamp stamp = node_->getTime();

                if (!ec)
                {
                    if (numBytes == 1)
                    {
                        uint8_t& currByte = telegram_->message[index];

                        if (currByte == SYNC_BYTE_1)
                        {
                            telegram_->stamp = stamp;
                            readSync<1>();
                        } else
                        {
                            switch (index)
                            {
                            case 0:
                            {
                                telegram_->type = telegram_type::UNKNOWN;
                                readUnknown();
                                break;
                            }
                            case 1:
                            {
                                switch (currByte)
                                {
                                case SBF_SYNC_BYTE_2:
                                {
                                    telegram_->type = telegram_type::SBF;
                                    readSbfHeader();
                                    break;
                                }
                                case NMEA_SYNC_BYTE_2:
                                {
                                    telegram_->type = telegram_type::NMEA;
                                    readSync<2>();
                                    break;
                                }
                                case NMEA_INS_SYNC_BYTE_2:
                                {
                                    telegram_->type = telegram_type::NMEA_INS;
                                    readSync<2>();
                                    break;
                                }
                                case RESPONSE_SYNC_BYTE_2:
                                {
                                    telegram_->type = telegram_type::RESPONSE;
                                    readSync<2>();
                                    break;
                                }
                                default:
                                {
                                    std::stringstream ss;
                                    ss << std::hex << currByte;
                                    node_->log(
                                        log_level::DEBUG,
                                        "AsyncManager sync byte 2 read fault, should never come here.. Received byte was " +
                                            ss.str());
                                    resync();
                                    break;
                                }
                                }
                                break;
                            }
                            case 2:
                            {
                                switch (currByte)
                                {
                                case NMEA_SYNC_BYTE_3:
                                {
                                    if (telegram_->type == telegram_type::NMEA)
                                        readString();
                                    else
                                        resync();
                                    break;
                                }
                                case NMEA_INS_SYNC_BYTE_3:
                                {
                                    if (telegram_->type == telegram_type::NMEA_INS)
                                        readString();
                                    else
                                        resync();
                                    break;
                                }
                                case RESPONSE_SYNC_BYTE_3:
                                {
                                    if (telegram_->type == telegram_type::RESPONSE)
                                        readString();
                                    else
                                        resync();
                                    break;
                                }
                                case RESPONSE_SYNC_BYTE_3a:
                                {
                                    if (telegram_->type == telegram_type::RESPONSE)
                                        readString();
                                    else
                                        resync();
                                    break;
                                }
                                case ERROR_SYNC_BYTE_3:
                                {
                                    if (telegram_->type == telegram_type::RESPONSE)
                                    {
                                        telegram_->type =
                                            telegram_type::ERROR_RESPONSE;
                                        readString();
                                    } else
                                        resync();
                                    break;
                                }
                                default:
                                {
                                    std::stringstream ss;
                                    ss << std::hex << currByte;
                                    node_->log(
                                        log_level::DEBUG,
                                        "AsyncManager sync byte 3 read fault, should never come here. Received byte was " +
                                            ss.str());
                                    resync();
                                    break;
                                }
                                }
                                break;
                            }
                            default:
                            {
                                node_->log(
                                    log_level::DEBUG,
                                    "AsyncManager sync read fault, should never come here.");
                                resync();
                                break;
                            }
                            }
                        }
                    } else
                    {
                        node_->log(
                            log_level::DEBUG,
                            "AsyncManager sync read fault, wrong number of bytes read: " +
                                std::to_string(numBytes));
                        resync();
                    }
                } else
                {
                    node_->log(log_level::DEBUG,
                               "AsyncManager sync read error: " + ec.message());
                    resync();
                }
            });
    }

    template <typename IoType>
    void AsyncManager<IoType>::readSbfHeader()
    {
        telegram_->message.resize(SBF_HEADER_SIZE);

        boost::asio::async_read(
            *(ioInterface_.stream_),
            boost::asio::buffer(telegram_->message.data() + 2, SBF_HEADER_SIZE - 2),
            [this](boost::system::error_code ec, std::size_t numBytes) {
                if (!ec)
                {
                    if (numBytes == (SBF_HEADER_SIZE - 2))
                    {
                        uint16_t length =
                            parsing_utilities::getLength(telegram_->message);
                        if (length > MAX_SBF_SIZE)
                        {
                            node_->log(
                                log_level::DEBUG,
                                "AsyncManager SBF header read fault, length of block exceeds " +
                                    std::to_string(MAX_SBF_SIZE) + ": " +
                                    std::to_string(length));
                        } else
                            readSbf(length);
                    } else
                    {
                        node_->log(
                            log_level::DEBUG,
                            "AsyncManager SBF header read fault, wrong number of bytes read: " +
                                std::to_string(numBytes));
                        resync();
                    }
                } else
                {
                    node_->log(log_level::DEBUG,
                               "AsyncManager SBF header read error: " +
                                   ec.message());
                    resync();
                }
            });
    }

    template <typename IoType>
    void AsyncManager<IoType>::readSbf(std::size_t length)
    {
        telegram_->message.resize(length);

        boost::asio::async_read(
            *(ioInterface_.stream_),
            boost::asio::buffer(telegram_->message.data() + SBF_HEADER_SIZE,
                                length - SBF_HEADER_SIZE),
            [this, length](boost::system::error_code ec, std::size_t numBytes) {
                if (!ec)
                {
                    if (numBytes == (length - SBF_HEADER_SIZE))
                    {
                        if (crc::isValid(telegram_->message))
                        {
                            telegramQueue_->push(telegram_);
                        } else
                            node_->log(log_level::DEBUG,
                                       "AsyncManager crc failed for SBF  " +
                                           std::to_string(parsing_utilities::getId(
                                               telegram_->message)) +
                                           ".");
                    } else
                    {
                        node_->log(
                            log_level::DEBUG,
                            "AsyncManager SBF read fault, wrong number of bytes read: " +
                                std::to_string(numBytes));
                    }
                    resync();
                } else
                {
                    node_->log(log_level::DEBUG,
                               "AsyncManager SBF read error: " + ec.message());
                    resync();
                }
            });
    }

    template <typename IoType>
    void AsyncManager<IoType>::readUnknown()
    {
        telegram_->message.resize(1);
        telegram_->message.reserve(256);
        readStringElements();
    }

    template <typename IoType>
    void AsyncManager<IoType>::readString()
    {
        telegram_->message.resize(3);
        telegram_->message.reserve(256);
        readStringElements();
    }

    template <typename IoType>
    void AsyncManager<IoType>::readStringElements()
    {
        boost::asio::async_read(
            *(ioInterface_.stream_), boost::asio::buffer(buf_.data(), 1),
            [this](boost::system::error_code ec, std::size_t numBytes) {
                if (!ec)
                {
                    if (numBytes == 1)
                    {
                        telegram_->message.push_back(buf_[0]);
                        /*node_->log(log_level::DEBUG,
                                   "Buffer: " +
                                       std::string(telegram_->message.begin(),
                                                   telegram_->message.end()));*/

                        switch (buf_[0])
                        {
                        case SYNC_BYTE_1:
                        {
                            telegram_.reset(new Telegram);
                            telegram_->message[0] = buf_[0];
                            telegram_->stamp = node_->getTime();
                            node_->log(
                                log_level::DEBUG,
                                "AsyncManager string read fault, sync 1 found.");
                            readSync<1>();
                            break;
                        }
                        case LF:
                        {
                            if (telegram_->message[telegram_->message.size() - 2] ==
                                CR)
                                telegramQueue_->push(telegram_);
                            else
                                node_->log(
                                    log_level::DEBUG,
                                    "LF wo CR: " +
                                        std::string(telegram_->message.begin(),
                                                    telegram_->message.end()));
                            resync();
                            break;
                        }
                        case CONNECTION_DESCRIPTOR_FOOTER:
                        {
                            telegram_->type = telegram_type::CONNECTION_DESCRIPTOR;
                            telegramQueue_->push(telegram_);
                            resync();
                            break;
                        }
                        default:
                        {
                            readStringElements();
                            break;
                        }
                        }
                    } else
                    {
                        node_->log(
                            log_level::DEBUG,
                            "AsyncManager string read fault, wrong number of bytes read: " +
                                std::to_string(numBytes));
                        resync();
                    }
                } else
                {
                    node_->log(log_level::DEBUG,
                               "AsyncManager string read error: " + ec.message());
                    resync();
                }
            });
    }
} // namespace io