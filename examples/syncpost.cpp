#include <experimental/net>
#include <string>
#include <iostream>
#include <system_error>

using namespace std::literals::string_literals;
namespace net = std::experimental::net;
using net::ip::tcp;

int main()
{
   net::io_context io_context;
   tcp::socket socket(io_context);
   tcp::resolver resolver(io_context);

   net::connect(socket,
                resolver.resolve("cpp.ciere.cloud", "8001"));

   std::string form = "{ \"name\":\"Kirk Shoop\", \"psk\":\"hidden\"}"s;

   for(auto v : { "POST /register HTTP/1.1\r\n"s
                , "Host: cpp.ciere.cloud:8000\r\n"s
                , "Content-Type: application/json\r\n"s
                , "Content-Length: "s + std::to_string(form.size()) + "\r\n"s
                , "Cache-Control: no-cache\r\n"s
                , "Connection: close\r\n"s
                , "\r\n"s
                , form } )
   {
      std::cout << v;
      net::write(socket, net::buffer(v));
   }
   std::cout << std::endl;

   std::string header;
   net::read(socket,
             net::dynamic_buffer(header),
             [&header](auto ec, auto n) -> std::size_t
             {
                if(ec ||
                   (   header.size() > 3
                    && header.compare(header.size()-4, 4,
                                      "\r\n\r\n") == 0 ))
                {
                   return 0;
                }
                return 1;
             });

   std::error_code e;
   std::string body;

   net::read(socket,
             net::dynamic_buffer(body),
             e);

   std::cout << header << std::endl;
   std::cout << body << std::endl;
}
