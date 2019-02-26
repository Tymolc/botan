/*
* TLS Stream Helper
* (C) 2018-2019 Jack Lloyd
*     2018-2019 Hannes Rantzsch, Tim Oesterreich, Rene Meusel
*
* Botan is released under the Simplified BSD License (see license.txt)
*/

#ifndef BOTAN_ASIO_ASYNC_HANDSHAKE_OP_H_
#define BOTAN_ASIO_ASYNC_HANDSHAKE_OP_H_

#include <botan/internal/asio_async_write_op.h>
#include <botan/internal/asio_convert_exceptions.h>
#include <botan/internal/asio_stream_core.h>
#include <botan/internal/asio_includes.h>

namespace Botan {

namespace TLS {

template <class Handler, class Stream, class Allocator = std::allocator<void>>
struct AsyncHandshakeOperation : public AsyncBase<Handler, typename Stream::executor_type, Allocator>
   {
      template<class HandlerT>
      AsyncHandshakeOperation(
         HandlerT&& handler,
         Stream& stream,
         StreamCore& core)
         : AsyncBase<Handler, typename Stream::executor_type, Allocator>(
              std::forward<HandlerT>(handler),
              stream.get_executor())
         , m_stream(stream)
         , m_core(core)
         {
         }

      AsyncHandshakeOperation(AsyncHandshakeOperation&&) = default;

      using typename AsyncBase<Handler, typename Stream::executor_type, Allocator>::allocator_type;
      using typename AsyncBase<Handler, typename Stream::executor_type, Allocator>::executor_type;

      void operator()(boost::system::error_code ec, std::size_t bytesTransferred, bool isContinuation = true)
         {
         // process tls packets from socket first
         if(bytesTransferred > 0)
            {
            boost::asio::const_buffer read_buffer {m_core.input_buffer.data(), bytesTransferred};
            try
               {
               m_stream.native_handle()->received_data(static_cast<const uint8_t*>(read_buffer.data()), read_buffer.size());
               }
            catch(const std::exception&)
               {
               ec = convertException();
               this->invoke(isContinuation, ec);
               return;
               }
            }

         // send tls packets
         if(m_core.hasDataToSend())
            {
            // TODO comment: plainBytesTransferred is 0 here because...  we construct a write operation to use it only
            // as a handler for our async_write call, not for actually calling it.  However, once
            // AsyncWriteOperation::operator() is called as the handler, it will consume the send buffer and call (this)
            // as it's own handler. Now we know that AsyncHandshakeOperation::operator() checks bytesTransferred first.
            // We want it to NOT receive data into the channel, hence we set plainBytesTransferred to 0 here.
            AsyncWriteOperation<
            AsyncHandshakeOperation<typename std::decay<Handler>::type, Stream, Allocator>,
                                    Stream,
                                    Allocator>
                                    op{std::move(*this), m_stream, m_core, 0};
            boost::asio::async_write(m_stream.next_layer(), m_core.sendBuffer(), std::move(op));
            return;
            }

         // we need more tls data from the socket
         if(!m_stream.native_handle()->is_active() && !ec)
            {
            m_stream.next_layer().async_read_some(m_core.input_buffer, std::move(*this));
            return;
            }

         this->invoke(isContinuation, ec);
         }

   private:
      Stream&      m_stream;
      StreamCore&  m_core;
   };

}  // namespace TLS

}  // namespace Botan

#endif
