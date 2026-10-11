# WebFront networking provider

`TCPNetworkingStdnet.hpp` provides `webfront::networking::TCPStdnet`, a replacement for
`TCPNetworkingTS` that runs WebFront's HTTP server and WebSockets on stdnet.

## Integration

1. Copy `TCPNetworkingStdnet.hpp` into WebFront's `include/networking/`.
2. In WebFront's `CMakeLists.txt`, replace the Networking TS package with stdnet:

   ```cmake
   CPMAddPackage("gh:ambroise-leclerc/stdnet#main")
   target_link_libraries(WebFront INTERFACE stdnet::stdnet)
   ```

3. Select the provider:

   ```cpp
   #include <networking/TCPNetworkingStdnet.hpp>
   using NetProvider = webfront::networking::TCPStdnet;
   ```

`WebFront.hpp` currently includes `TCPNetworkingTS.hpp` unconditionally; that include (and
the Networking TS dependency) can go once the default `NetProvider` is switched.

## Behaviour notes

* Completion handlers keep the Networking TS signatures: `(std::error_code, std::size_t)` for
  reads and writes, `(std::error_code, Socket)` for accepts.
* stdnet reports end of stream as a zero-byte read; the provider maps it back to an error
  (`errc::connection_reset`) because WebFront's read loops treat "no error" as "data arrived".
* `Error::OperationAborted` is `errc::operation_canceled`, which is what pending operations
  complete with after `socket.close()` or `acceptor.close()`.
* `io_context::run()` returns when no operation is pending, as with the Networking TS; the
  server's pending accept keeps it running.

## Verified

Built against WebFront's `BasicWF<TCPStdnet, fs::NativeDebugFS>`: static files served over
HTTP (100 concurrent requests) and WebSocket upgrade (`101 Switching Protocols`).
