/* vim:set noexpandtab tabstop=4 wrap filetype=cpp */
#ifndef RECEIVESQL_H
#define RECEIVESQL_H

// for passing input variables
#include "Store.h"
// for finding clients
#include "ServiceDiscovery.h"
#include "Utilities.h"
// for databse interaction
#include "Postgres.h"
#include <pqxx/pqxx>
// wrapper class for zmq messages
#include "Query.h"
// wrapper class for log messages
#include "LogMsg.h"
// for network comms
#include <zmq.hpp>
// for keeping track of elapsed time durations
#include <boost/date_time/posix_time/posix_time.hpp>
#include <time.h>  // for 'struct tm'
// general
#include <string>
#include <iostream>
#include <deque>

class ReceiveSQL{
	public:
	ReceiveSQL(){};
	~ReceiveSQL(){};
	
	bool Initialise(std::string configfile);
	bool InitPostgres(Store& m_variables, std::string prefix);
	bool InitZMQ(Store& m_variables);
	bool InitMessaging(Store& m_variables);
	bool InitServiceDiscovery(Store& m_variables);
	
	bool Execute();
	bool FindNewClients();
	bool GetClientWriteQueries();
	bool GetClientReadQueries();
	bool GetClientLogMessages();
	bool GetMiddlemanCheckin();
	bool CheckMasterStatus();
	bool RunNextWriteQuery();
	bool RunNextReadQuery();
	bool RunNextLogMsg();
	bool SendNextReply();
	bool SendNextLogMsg();
	bool BroadcastPresence();
	bool CleanupCache();
	bool TrimQueue(std::string queuename);
	bool TrimDequeue(std::string queuename);
	bool TrimCache();
	bool TrackStats();
	
	bool Finalise();
	
	bool NegotiateMaster(std::string their_header="", std::string their_timestamp="");
	bool NegotiationRequest();
	bool NegotiationReply(std::string their_header, std::string their_timestamp);
	bool UpdateRole();
	boost::posix_time::ptime ToTimestamp(std::string timestring);
	std::string ToTimestring(boost::posix_time::ptime);
	bool GetLastUpdateTime(std::string& our_timestamp);
	
	// Logging functions
	bool Log(std::string message, uint32_t message_severity);
	bool LogToDb(LogMsg logmsg);
	
	// generic receive functions
	int PollAndReceive(zmq::socket_t* sock, zmq::pollitem_t poll, int timeout, std::vector<zmq::message_t>& outputs);
	bool Receive(zmq::socket_t* sock, std::vector<zmq::message_t>& outputs);
	
	private:
	// an instance of the postgres interface class to communicate with the run database
	Postgres m_rundb;
	Postgres m_monitoringdb;
	
	
	int stdio_verbosity;
	int db_verbosity;
	
	zmq::context_t* context=nullptr;
	
	// these receive connections from others;
	// ServiceDiscovery will invoke 'connect' for us
	zmq::socket_t* clt_rtr_socket=nullptr;  // receives read queries from client dealers
	zmq::socket_t* clt_sub_socket=nullptr;  // receives write queries from client publishers
	zmq::socket_t* mm_rcv_socket=nullptr;   // receives connections from other middlemen
	zmq::socket_t* log_sub_socket=nullptr;  // receives log messages from client publishers
	
	// these sockets will bind, they advertise our services
	zmq::socket_t* mm_snd_socket=nullptr;   // we will advertise our presence as a middleman to other middlemen
	zmq::socket_t* log_pub_socket=nullptr;  // we will advertise our presence as a source of logging
	
	// Service Discovery finds clients that are interested in our services
	// and connect us to those sockets
	ServiceDiscovery* service_discovery = nullptr;
	Utilities* utilities = nullptr;
	// required by the Utilities class to keep track of connections to clients
	// we should have one map per zmq_socket managed by the Utilities class;
	// it uses this to determine if we are connected to a given client already
	std::map<std::string,Store*> clt_rtr_connections;
	std::map<std::string,Store*> mm_rcv_connections;
	std::map<std::string,Store*> clt_sub_connections;
	std::map<std::string,Store*> log_sub_connections;
	
	// poll timeouts
	int inpoll_timeout;
	int outpoll_timeout;
	
	// bundle the polls together so we can do all of them at once
	std::vector<zmq::pollitem_t> in_polls;
	std::vector<zmq::pollitem_t> out_polls;
	
	// we need to keep some extra socket options around, for sockets that only the master uses.
	// these sockets get deleted and re-constructed as we get demoted/promoted, and we will need
	// their variables to re-set the socket properties.
	// (afaict you can't just disconnect and reconnect a socket...)
	int log_sub_port;
	int clt_sub_port;
	int log_pub_port;
	int clt_sub_socket_timeout;
	int log_sub_socket_timeout;
	
	// also keep this since we need to pass it from InitZMQ to InitServiceDiscovery
	int mm_snd_port;
	
	// Master-Slave variables
	// ######################
	bool am_master;
	bool dont_promote; // keep in standby mode
	bool warn_no_standby;
	// middleman check-in frequency
	boost::posix_time::time_duration broadcast_period;
	// how long we go without a message from the master before we promote ourselves
	boost::posix_time::time_duration promote_timeout;
	// when we last send our middleman broadcast message
	boost::posix_time::ptime last_mm_send;
	// when we last heard from the other middleman
	boost::posix_time::ptime last_mm_receipt;
	// how long before we start warning that the other middleman has been silent
	int mm_warn_timeout;
	
	// Client messaging variables
	// ##########################
	int max_send_attempts;   // how many times we try to send a response if zmq fails
	int warn_limit;          // how many postgres queries/responses to queue before we start dropping them
	int drop_limit;          // how many postgres queries/responses to queue before we emit warnings
	boost::posix_time::time_duration cache_period;  // how long to retain client responses for possible resending
	// three message queues:
	// 1. a queue of sql queries we're to enact
	// 2. a queue of responses to send to clients
	// 3. a queue of logging messages to send to the master monitoring db
	std::map<std::pair<std::string, uint32_t>, Query> wrt_txn_queue;
	std::map<std::pair<std::string, uint32_t>, Query> rd_txn_queue;
	std::map<std::pair<std::string, uint32_t>, Query> resp_queue;
	std::deque<LogMsg> in_log_queue;
	std::deque<LogMsg> out_log_queue;
	// we'll cache a set of recent responses send to each client,
	// then if a client that misses their acknowledgement and resends the query,
	// we can resend the response without re-running the query.
	// this is mostly important to prevent repeated runs of 'insert' queries
	std::map<std::pair<std::string, uint32_t>, Query> cache;
	
	// Negotiation variables
	// #####################
	int negotiate_period;
	int negotiation_timeout;
	
	// Misc variables
	// ##############
	std::string my_id; // client ID for logging.
	// if we get a write query over the dealer port, which should be used for read-only transactions,
	// do we just error out, or, if we're the master, do we just run it anyway...?
	bool handle_unexpected_writes = false;
	
	// generally useful variable for getting return values
	bool get_ok;
	// a general elapsed time variable we can re-use in calculations
	boost::posix_time::time_duration elapsed_time;
	
	// Monitoring stats.
	// number of messages received over zmq sockets, and how many failed.
	unsigned long write_queries_recvd;
	unsigned long write_query_recv_fails;
	unsigned long read_queries_recvd;
	unsigned long read_query_recv_fails;
	unsigned long log_msgs_recvd;
	unsigned long log_msg_recv_fails;
	unsigned long mm_broadcasts_recvd;
	unsigned long mm_broadcast_recv_fails;
	
	// number of postgres queries that failed to run
	unsigned long write_queries_failed;
	unsigned long read_queries_failed;
	
	// number of postgres monitoring insertions that failed to run
	unsigned long in_logs_failed;
	
	// number of messages sent over zmq sockets, and how many failed.
	unsigned long acks_sent;
	unsigned long ack_send_fails;
	unsigned long log_msgs_sent;
	unsigned long log_send_fails;
	unsigned long mm_broadcasts_sent;
	unsigned long mm_broadcasts_failed;
	
	// number of times we've had role conflicts
	unsigned long master_clashes;
	unsigned long standby_clashes;
	// number of times we've failed to successfully negotiation
	unsigned long master_clashes_failed;
	unsigned long standby_clashes_failed;
	// number of times master went silent
	unsigned long self_promotions;
	unsigned long self_promotions_failed;
	
	// number of times we've changed roles
	unsigned long promotions;
	unsigned long promotions_failed;
	unsigned long demotions;
	unsigned long demotions_failed;
	
	// number of messages we've dropped from queues due to overflow
	unsigned long dropped_writes;
	unsigned long dropped_reads;
	unsigned long dropped_acks;
	// number of log messages we've fropped from queues due to overflow
	unsigned long dropped_logs_out;
	unsigned long dropped_logs_in;
	
	// how often to calculate stats
	boost::posix_time::time_duration stats_period;
	
	// calculated rates. Somewhat redundant. includes all received messages over the corresponding port,
	// even if the recieve failed, or it was a duplicate, or the query was dropped, or it failed.
	unsigned long read_query_rate;
	unsigned long write_query_rate;
	// a timestamp since last statistics calculation to determine the rate
	boost::posix_time::ptime last_stats_calc;
	
	// for holding stats variables and turning them into a json
	Store MonitoringStore;
	
	////////////
	// variadic templates: our excuse to use c++11 ;)
	
	// the following are wrappers to accept any number of arguments of arbitrary types, and sending them
	// over a zmq socket as a multi-part message. It encapsulates copying the data into zmq::messge_t,
	// setting the ZMQ_SNDMORE flag, checking each part sends and abandoning at the first failure.
	// optionally it may also poll the output socket first.
	
	// base cases; send single (final) message part
	// 1. case where we're given a zmq::message_t -> just send it
	bool Send(zmq::socket_t* sock, bool more, zmq::message_t& message);
	// 2. case where we're given a std::string -> specialise accessing underlying data
	bool Send(zmq::socket_t* sock, bool more, std::string messagedata);
	// 3. case where we're given a vector of strings
	bool Send(zmq::socket_t* sock, bool more, std::vector<std::string> messages);
	// 4. generic case for other primitive types -> relies on &messagedata and sizeof(T) being suitable.
	template <typename T>
	bool Send(zmq::socket_t* sock, bool more, T&& messagedata){
		zmq::message_t message(sizeof(T));
		memcpy(message.data(), &messagedata, sizeof(T));
		bool send_ok;
		if(more) send_ok = sock->send(message, ZMQ_SNDMORE);
		else     send_ok = sock->send(message);
		return send_ok;
	}
	
	// recursive case; send the next message part and forward all remaining parts
	template <typename T, typename... Rest>
	bool Send(zmq::socket_t* sock, bool more, T&& message, Rest&&... rest){
		bool send_ok = Send(sock, true, std::forward<T>(message));
		if(not send_ok) return false;
		return Send(sock, false, std::forward<Rest>(rest)...);
	}
	
	// wrapper to do polling if required
	// version if one part
	template <typename T>
	int PollAndSend(zmq::socket_t* sock, zmq::pollitem_t poll, int timeout, T&& message){
		// check for listener
		int ret = zmq::poll(&poll, 1, timeout);
		if(ret<0){
			// error polling - is the socket closed?
			return -3;
		}
		if(poll.revents & ZMQ_POLLOUT){
			bool send_ok = Send(sock, false, std::forward<T>(message));
			if(not send_ok) return -1;
		} else {
			// no listener
			return -2;
		}
		return 0;
	}
	
	// wrapper to do polling if required
	// version if more than one part
	template <typename T, typename... Rest>
	int PollAndSend(zmq::socket_t* sock, zmq::pollitem_t poll, int timeout, T&& message, Rest&&... rest){
		// check for listener
		int ret = zmq::poll(&poll, 1, timeout);
		if(ret<0){
			// error polling - is the socket closed?
			return -3;
		}
		if(poll.revents & ZMQ_POLLOUT){
			bool send_ok = Send(sock, true, std::forward<T>(message), std::forward<Rest>(rest)...);
			if(not send_ok) return -1;
		} else {
			// no listener
			return -2;
		}
		return 0;
	}
	
	// handy helper function for building strings for log messages
	template <typename T>
	void AddPart(std::stringstream& message, T& next_part){
		message << next_part;
		return;
	}
	
	template <typename T, typename... Rest>
	void AddPart(std::stringstream& message, T& next_part, Rest... rest){
		message << next_part;
		return AddPart(message, rest...);
	}
	
	template <typename... Ts>
	std::string Concat(Ts... args){
		std::stringstream tmp;
		AddPart(tmp, args...);
		return tmp.str();
	}
};

#endif
