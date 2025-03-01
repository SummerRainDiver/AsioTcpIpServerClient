#pragma once

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/write.hpp>
#include <functional>
#include <iostream>
#include <string>

using boost::asio::steady_timer;
using boost::asio::ip::tcp;
using std::placeholders::_1;
using std::placeholders::_2;

using namespace std;

class client
{
public:
    client(boost::asio::io_context& io_context)
        : socket_(io_context),
        deadline_(io_context),
        heartbeat_timer_(io_context)
    {
    }

    void start(tcp::resolver::results_type endpoints)
    {
        endpoints_ = endpoints;
        start_connect(endpoints_.begin());

        // Start the deadline actor. You will note that we're not setting any
        // particular deadline here. Instead, the connect and input actors will
        // update the deadline prior to each asynchronous operation.
        deadline_.async_wait(std::bind(&client::check_deadline, this));
    }

    // This function terminates all the actors to shut down the connection. It
    // may be called by the user of the client class, or by the class itself in
    // response to graceful termination or an unrecoverable error.
    void stop()
    {
        stopped_ = true;
        boost::system::error_code ignored_error;
        socket_.close(ignored_error);
        deadline_.cancel();
        heartbeat_timer_.cancel();
    }

private:
    void start_connect(tcp::resolver::results_type::iterator endpoint_iter)
    {
        if (endpoint_iter != endpoints_.end())
        {
            std::cout << "Trying to connect Server [IP : " << endpoint_iter->endpoint() << " ]...\n";

            // Set a deadline for the connect operation.
            deadline_.expires_after(std::chrono::seconds(60));

            // Start the asynchronous connect operation.
            socket_.async_connect(endpoint_iter->endpoint(),
                std::bind(&client::handle_connect,
                    this, _1, endpoint_iter));
        }
        else
        {
            // There are no more endpoints to try. Shut down the client.
            stop();
        }
    }

    void handle_connect(const boost::system::error_code& error,
        tcp::resolver::results_type::iterator endpoint_iter)
    {
        if (stopped_)
            return;

        // The async_connect() function automatically opens the socket at the start
        // of the asynchronous operation. If the socket is closed at this time then
        // the timeout handler must have run first.
        if (!socket_.is_open())
        {
            std::cout << "Connect timed out\n";

            // Try the next available endpoint.
            start_connect(++endpoint_iter);
        }

        // Check if the connect operation failed before the deadline expired.
        else if (error)
        {
            std::cout << "Connect error: " << error.message() << "\n";

            // We need to close the socket used in the previous connection attempt
            // before starting a new one.
            socket_.close();

            // Try the next available endpoint.
            start_connect(++endpoint_iter);
        }

        // Otherwise we have successfully established a connection.
        else
        {
            std::cout << endpoint_iter->endpoint() << "Server connection successful..." << endl;

            // Start the input actor.
            //start_read();

            // Start the heartbeat actor.
            start_write();
        }
    }

    void start_read()
    {
        // Start an asynchronous operation to read a newline-delimited message.
        boost::asio::async_read_until(socket_,
            boost::asio::dynamic_buffer(input_buffer_), '\0',
            std::bind(&client::handle_read, this, _1, _2));
    }

    void handle_read(const boost::system::error_code& error, std::size_t n)
    {
        if (stopped_)
            return;

        if (!error)
        {
            // Extract the newline-delimited message from the buffer.
            std::string line(input_buffer_.substr(1, n));
            input_buffer_.erase(0, n);

            // Empty messages are heartbeats and so ignored.
            if (!line.empty())
            {
                cout << "Echo message from Server :" << line << endl;
            }

            start_write();
        }
        else
        {
            std::cout << "Error on receive: " << error.message() << "\n";

            stop();
        }
    }

    void start_write()
    {
        if (stopped_)
            return;

        // Start an asynchronous operation to send a heartbeat message.
        boost::asio::async_write(socket_, boost::asio::buffer("\n", 1),
            std::bind(&client::handle_write, this, _1));
    }

    void handle_write(const boost::system::error_code& error)
    {
        if (stopped_)
            return;

        if (!error)
        {
            // Wait 10 seconds before sending the next heartbeat.
            //heartbeat_timer_.expires_after(std::chrono::seconds(10));
            //heartbeat_timer_.async_wait(std::bind(&client::start_write, this));
            char message[128] = { 0, };
            cout << "Send Message to Server : ";
            cin >> message;
            strcat_s(message, "\0");
            int nMsgLen = strnlen_s(message, 128 - 1);
            boost::system::error_code ignored_error;
            socket_.write_some(boost::asio::buffer(message, nMsgLen + 1), ignored_error);

            start_read();
        }
        else
        {
            std::cout << "Error on heartbeat: " << error.message() << "\n";

            stop();
        }
    }

    void check_deadline()
    {
        if (stopped_)
            return;

        // Check whether the deadline has passed. We compare the deadline against
        // the current time since a new asynchronous operation may have moved the
        // deadline before this actor had a chance to run.
        if (deadline_.expiry() <= steady_timer::clock_type::now())
        {
            // The deadline has passed. The socket is closed so that any outstanding
            // asynchronous operations are cancelled.
            socket_.close();

            // There is no longer an active deadline. The expiry is set to the
            // maximum time point so that the actor takes no action until a new
            // deadline is set.
            deadline_.expires_at(steady_timer::time_point::max());
        }

        // Put the actor back to sleep.
        deadline_.async_wait(std::bind(&client::check_deadline, this));
    }

private:
    bool stopped_ = false;
    tcp::resolver::results_type endpoints_;
    tcp::socket socket_;
    std::string input_buffer_;
    steady_timer deadline_;
    steady_timer heartbeat_timer_;
};