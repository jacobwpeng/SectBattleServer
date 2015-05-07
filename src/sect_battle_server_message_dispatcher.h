/*
 * =============================================================================
 *
 *       Filename:  sect_battle_server_message_dispatcher.h
 *        Created:  04/27/15 17:24:40
 *         Author:  Peng Wang
 *          Email:  pw2191195@gmail.com
 *    Description:  
 *
 * =============================================================================
 */

#ifndef  __SECT_BATTLE_SERVER_MESSAGE_DISPATCHER_H__
#define  __SECT_BATTLE_SERVER_MESSAGE_DISPATCHER_H__

#include <map>
#include <memory>
#include <type_traits>
#include <functional>
#include <google/protobuf/message.h>

namespace SectBattle {
    class MessageCallback {
        public:
            virtual ~MessageCallback() = default;
            virtual ssize_t OnMessage(const google::protobuf::Message* m, char* out) = 0;
    };

    template<typename T>
    class ConcreteMessageCallback : public MessageCallback {
        public:
            static_assert(std::is_base_of<google::protobuf::Message, T>::value,
                    "T must derive from google::protobuf::Message");
            using MessageType = T;
            using CallbackType = std::function<ssize_t(const T*, char*)>;
            ConcreteMessageCallback(CallbackType cb)
                :cb_(cb) {
            }
            virtual ~ConcreteMessageCallback() = default;
            virtual ssize_t OnMessage(const google::protobuf::Message* m, char* out) {
                auto concrete = dynamic_cast<const T*>(m);
                return cb_(concrete, out);
            }

        private:
            CallbackType cb_;
    };

    class MessageDispatcher {
        public:
            ssize_t Dispatch(const google::protobuf::Message* m, char* out);
            template<typename T>
            void Register(const typename ConcreteMessageCallback<T>::CallbackType& cb) {
                std::unique_ptr<ConcreteMessageCallback<T>> callback(
                        new ConcreteMessageCallback<T>(cb));
                callbacks_[T::descriptor()] = std::move(callback);
            }

        private:
            std::map<const google::protobuf::Descriptor*,
                std::unique_ptr<MessageCallback>> callbacks_;
    };
};

#endif   /* ----- #ifndef __SECT_BATTLE_SERVER_MESSAGE_DISPATCHER_H__  ----- */
