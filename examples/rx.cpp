// c++ -std=c++14 -I ../networking-ts-impl-master/include/ -I ../rxcpp-master/ rx.cpp

#include <experimental/net>
#include <string>
#include <iostream>
#include <thread>
#include <system_error>
#include <future>
#include <tuple>
#include <regex>

#include <rxcpp/rx.hpp>
using namespace rxcpp;

using namespace std;
using namespace std::chrono_literals;
using namespace std::literals::string_literals;
namespace net = std::experimental::net;
using net::ip::tcp;

enum class Split {
    KeepDelimiter,
    RemoveDelimiter,
    OnlyDelimiter
};
auto split = [](string d, Split m = Split::KeepDelimiter){
    return [=](observable<string> in) -> observable<string> {
        return in |
            rxo::map([=](const string& s) {
                regex delim(d);
                cregex_token_iterator cursor(&s[0], &s[0] + s.size(), delim, m == Split::KeepDelimiter ? initializer_list<int>({-1, 0}) : (m == Split::RemoveDelimiter ? initializer_list<int>({-1}) : initializer_list<int>({0})));
                cregex_token_iterator end;
                vector<string> splits(cursor, end);
                return rxs::iterate(splits);
            }) |
            rxo::merge();
    };
};

struct state : enable_shared_from_this<state>
{
    state(net::io_context& c) : socket(c) {
    }
    tcp::socket socket;
    rxsub::subject<string> receive;
    string chunk;
};

struct connected
{
    shared_ptr<state> s;
};

auto rx_connect = [](net::io_context& io_context, const string& host, const string& service) {
    return observable<>::create<connected>([=, &io_context](subscriber<connected> out){

        auto s = std::make_shared<state>(io_context);

        tcp::resolver resolver(io_context);

        s->receive.get_subscriber().add([=](){
            cout << endl << "close" << endl;

            std::error_code close_ec;
            s->socket.close(close_ec);
        });

        net::async_connect(s->socket,
            resolver.resolve(host, service),
            [=](auto ec, auto ) {
                if(!ec) {
                    out.on_next(connected{s});
                    out.on_completed();
                }
                else {
                    out.on_error(std::make_exception_ptr(ec));
                }
        });
    });
};

auto rx_send = [](connected c) {
    return [=](observable<string> in) -> observable<connected> {
        return in |
            rxo::map([=](string s){
                return observable<>::create<connected>([=](subscriber<connected> out){
                    c.s->socket.async_send(net::buffer(s),
                        [=](auto ec, auto){
                            if(ec) {
                                out.on_error(std::make_exception_ptr(ec));
                            } else {
                                out.on_completed();
                            }
                    } );
                });
            }) |
            rxo::concat() |
            rxo::concat(rxs::just(c));
    };
};

struct reader
{
    shared_ptr<state> s;

    void operator()(error_code ec, int bytes_trans) {
        auto out = s->receive.get_subscriber();

        if(!ec || ec == net::stream_errc::eof) {
            out.on_next(s->chunk);
            s->chunk.clear();
        }

        if (ec == net::stream_errc::eof) {
            out.on_completed();
        } else if (ec) {
            out.on_error(std::make_exception_ptr(ec));
        } else {
            async_read();
        }
    }

    void async_read() {
        s->chunk.resize(1024 * 4); // workaround for broken dynamic_buffer
        s->socket.async_read_some(net::buffer(s->chunk), *this);
    }
};

auto rx_read = [](connected c) {
    return observable<>::create<observable<string>>([=](subscriber<observable<string>> out){

        out.on_next(c.s->receive.get_observable());
        out.on_completed();

        reader r{c.s};
        r.async_read();
    }) |
    rxo::concat();
};


int main()
{
    net::io_context io_context;
    auto work = net::make_work_guard(io_context);
    std::thread t([&io_context](){io_context.run();});

    auto host = "www.boost.org"s;
    auto service = "http"s;
    auto request = "GET / HTTP/1.0\r\n"
                    "Host: www.boost.org\r\n"
                    "Accept: */*\r\n"
                    "Connection: close\r\n\r\n"s;

    auto boostorg = rx_connect(io_context, host, service) |
        rxo::concat_map([=](connected c){
            return rxs::just(request) | 
                rx_send(c); 
        }) |
        rxo::concat_map([=](connected c){
            return rx_read(c);
        }) |
        rxo::publish() |
        rxo::ref_count();

    auto headers = boostorg |
        split("\r\n", Split::RemoveDelimiter) |
        rxo::take_while([](const string& s){
            return !s.empty();
        });

    auto size = boostorg |
        rxo::scan(0, [](int total, const string& chunk){
            return total + int(chunk.size());
        });

    auto responseupdates = boostorg |
        rxo::map([](const string&){return "response"s;});

    headers.
        concat(rxs::just("END HEADERS"s), responseupdates).
        with_latest_from(size | rxo::start_with(0)).
        as_blocking().
        subscribe(rxu::apply_to([](const string& s, int size){
            cout << s << " : " << size << endl;
        }), [](exception_ptr ex){
            cerr << rxu::what(ex) << endl;
        }, [](){
            cout << endl;
        });

    auto echo = rx_connect(io_context, "localhost", "20") |
        rxo::concat_map([=](connected c){
            return rx_read(c) | 
                rx_send(c); 
        }) |
        rxo::subscribe<connected>();

    io_context.stop();
    t.join();
}
