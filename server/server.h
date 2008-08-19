// Copyright (C) 2006-2007 David Sugar, Tycho Softworks.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include <sipwitch/sipwitch.h>
#include <eXosip2/eXosip.h>
#include <config.h>

#ifdef	_MSWINDOWS_
#undef	USES_COMMANDS
#endif

#ifndef	SESSION_EXPIRES
#define	SESSION_EXPIRES	"session-expires"
#endif

#ifndef	ALLOW_EVENTS
#define	ALLOW_EVENTS	"allow-events"
#endif

#define	SECOND_TIMEOUT	(1000l)
#define	MINUTE_TIMEOUT	(SECOND_TIMEOUT * 60l)
#define	HOUR_TIMEOUT	(MINUTE_TIMEOUT * 60l)
#define	DAY_TIMEOUT		(HOUR_TIMEOUT * 24l)

NAMESPACE_SIPWITCH
using namespace UCOMMON_NAMESPACE;

#define	CONFIG_KEY_SIZE 177
#define	PAGING_SIZE	(2048 * sizeof(void *))

#define	ENABLE_ALLOW_INVITE		0x0001
#define	ENABLE_ALLOW_ACK		0x0002
#define	ENABLE_ALLOW_CANCEL		0x0004
#define	ENABLE_ALLOW_REFER		0x0008
#define	ENABLE_ALLOW_OPTIONS	0x0010
#define	ENABLE_ALLOW_NOTIFY		0x0020
#define	ENABLE_ALLOW_SUBSCRIBE	0x0040
#define	ENABLE_ALLOW_PRACK		0x0080
#define	ENABLE_ALLOW_MESSAGE	0x0100
#define	ENABLE_ALLOW_INFO		0x0200

class thread;

class __LOCAL registry : private service::callback, private mapped_reuse<MappedRegistry> 
{ 
public: 
	class __LOCAL mapped : public MappedRegistry
	{
	public:
		void incUse(void);
		void decUse(void);
		bool expire(Socket::address& addr);
		bool refresh(Socket::address& addr, time_t expires);
		unsigned setTargets(Socket::address& addr);
		unsigned addTarget(Socket::address& via, time_t expires, const char *contact);
		unsigned setTarget(Socket::address& via, time_t expires, const char *contact);
		void addContact(const char *id);
		void addPublished(const char *id);
		void addRoute(const char *pat, unsigned pri, const char *prefix, const char *suffix);
	};

	class __LOCAL pointer
	{
	private:
		mapped *entry;

	public:
		pointer();
		pointer(const char *id);
		pointer(pointer const &copy);
		~pointer();
		
		void operator=(mapped *ptr);
		
		inline operator bool()
			{return entry != NULL;};

		inline bool operator!()
			{return entry == NULL;};

		inline mapped *operator->()
			{return entry;};

		inline mapped *operator*()
			{return entry;};
	};

	class __LOCAL target : public LinkedObject 
	{ 
	public: 
		// internal hidden address indexing object
		class indexing : public LinkedObject
		{
		public:
			struct sockaddr *address;
			mapped *registry;
			target *getTarget(void);
		} index;
		struct sockaddr_internet address;
		struct sockaddr_internet iface; 
		volatile time_t expires;
		enum {READY, BUSY, AWAY, DND} status;
		char contact[MAX_URI_SIZE]; 

		static void *operator new(size_t size);
		static void operator delete(void *ptr);
	};

	class __LOCAL pattern : public LinkedObject
	{
	public:
		mapped *registry;
		unsigned priority;
		char text[MAX_USERID_SIZE];
		char prefix[MAX_USERID_SIZE];
		char suffix[MAX_USERID_SIZE];
	};

private:
	class __LOCAL route : public LinkedObject
	{
	public:
		pattern entry;

		static void *operator new(size_t size);
		static void operator delete(void *ptr);
	};

	bool check(void);
	bool reload(service *cfg);
	void start(service *cfg);
	void stop(service *cfg);
	void snapshot(FILE *fp);

	static void expire(mapped *rr);
	static mapped *find(const char *id);

	static registry reg;

	volatile time_t expires;

	String digest;
	String realm;
	unsigned prefix;
	unsigned range;
	unsigned routes;

public:
	registry();

	inline static const char *getRealm(void)
		{return reg.realm;};

	inline static const char *getDigest(void)
		{return reg.digest;};

	inline static time_t getExpires(void)
		{return reg.expires;};

	inline static unsigned getPrefix(void)
		{return reg.prefix;};

	inline static unsigned getRange(void)
		{return reg.range;};

	inline static unsigned getRoutes(void)
		{return reg.routes;};


	static unsigned getEntries(void);
	static unsigned getIndex(mapped *rr);
	static bool isExtension(const char *id);
	static bool isUserid(const char *id);
	static mapped *address(struct sockaddr *addr);
	static mapped *contact(const char *uri);
	static mapped *contact(struct sockaddr *addr, const char *uid);
	static mapped *getExtension(const char *id);
	static mapped *create(const char *id);
	static mapped *access(const char *id);
	static mapped *invite(const char *id);
	static bool exists(const char *id);
	static pattern *getRouting(unsigned trs, const char *id);
	static void detach(mapped *m);
	static bool remove(const char *id);
	static void cleanup(time_t period); 
};

class __LOCAL stack : private service::callback, private mapped_reuse<MappedCall>, public TimerQueue
{
private:
	friend class proxy;
	friend class thread;
	friend class messages;

	class call;

	class __LOCAL background : public DetachedThread, public Conditional
	{
	public:
		static void create(timeout_t interval);
		static void cancel(void);

		static background *thread;

		static void modify(void);
		static void signal(void);
	

	private:
		bool cancelled;
		bool signalled;
		timeout_t interval;
		Timer expires;

		background(timeout_t sync);
		void run(void);
	};

	class __LOCAL session : public LinkedObject
	{
	public:
		registry::mapped *reg;
		int cid, tid, did;
		time_t activates;
		uint32_t sequence;
		call *parent;
		struct sockaddr_internet iface;
		time_t expires;					// session/invite expires...
		time_t ringing;					// ring no-answer timer...

		enum {OPEN, CLOSED, RING, BUSY, REORDER, REFER} state;

		// proxy info...
		rtpproxy::type_t proxying;
		char network[16];				// network segment used

		char sdp[1024];					// sdp body to use in exchange
		char identity[MAX_URI_SIZE];	// our effective contact/to point...
		char sysident[MAX_IDENT_SIZE];	// ident of this session
		char display[MAX_DISPLAY_SIZE];	// callerid reference field
		char from[MAX_URI_SIZE + MAX_DISPLAY_SIZE];	// formatted from line for endpoint

		char authid[MAX_USERID_SIZE];	// for authentication...
		char secret[MAX_USERID_SIZE];
		enum {NONE, DIGEST}	authtype;

		inline bool isSource(void)
			{return (this == parent->source);};

		inline bool isTarget(void)
			{return (this == parent->target);};
	};

	class __LOCAL segment : public OrderedObject
	{
	public:
		segment(call *cr, int cid, int did = -1, int tid = 0);

		static void *operator new(size_t size);
		static void operator delete(void *obj);

		session sid;

		inline session *get(void)
			{return &sid;};
	};

	class __LOCAL call : public TimerQueue::event
	{
	public:
		enum {DIRECTED, CIRCULAR, TERMINAL, REDIRECTED, DISTRIBUTED} mode;

		enum {LOCAL, INCOMING, OUTGOING, REFER} type;

		enum {INITIAL, TRYING, RINGING, RINGBACK, REORDER, HOLDING, ANSWERED, JOINED, TRANSFER, REDIRECT, BUSY, TERMINATE, FAILED, FINAL} state;

		call();

		unsigned fwdmask;				// forwarding mask in effect...
		char forward[MAX_IDENT_SIZE];	// ref id for forwarding...
		char dialed[MAX_IDENT_SIZE];	// user or ip address...
		char subject[MAX_URI_SIZE];		// call subject
		char refer[MAX_IDENT_SIZE];		// used in forward management
		rtpproxy *rtp;

		void reply_source(int error);
		void decline(thread *thread);
		void ring(thread *thread, session *s = NULL);
		void busy(thread *thread, session *s = NULL);
		void failed(thread *thread, session *s);
		void answer(thread *thread, session *s);
		void call_reply(thread *thread, session *s);
		void message_reply(thread *thread, session *s);
		void reinvite(thread *thread, session *s);
		void trying(thread *thread);
		void confirm(thread *thread, session *s);
		void expired(void);
		void closingLocked(session *s);
		void terminateLocked(void);
		void disconnectLocked(void);
		void joinLocked(session *s);
		void log(void);
		void bye(thread *thread, session *s);

		OrderedIndex segments;
		const char *reason;
		const char *joined;
		session *source;
		session *target;
		segment *select;
		MappedCall *map;
		enum {FWD_IGNORE, FWD_NA, FWD_BUSY, FWD_DND, FWD_AWAY, FWD_ALL} forwarding;
		unsigned count;			// total open segments
		unsigned invited;		// pending segments with invites
		unsigned ringing;		// number of ringing segments
		unsigned ringbusy;		// number of busy segments
		unsigned unreachable;	// number of unreachable segments
		time_t expires, starting, ending;
		int experror;			// error at expiration...
		bool phone;

		static void *operator new(size_t size);
		static void operator delete(void *obj);
	};

	bool reload(service *cfg);
	void start(service *cfg);
	void stop(service *cfg);
	void snapshot(FILE *fp);
	bool check(void);
	void update(void);
	void modify(void);

	unsigned threading, priority;
	size_t stacksize;

	volatile int timing;

	const char *volatile localnames;
	const char *volatile restricted;
	const char *volatile trusted;
	const char *volatile published;
	const char *volatile proxy;
	const char *iface;
	String agent;
	String system;
	String anon;
	short port;
	bool incoming, outgoing, dumping;
	int send101;
	timeout_t ring_timer, cfna_timer, reset_timer;
	unsigned invite_expires;

	int family, tlsmode, protocol;

	static stack sip;

public:
	stack();

	static const char *getScheme(void);
	static void getInterface(struct sockaddr *iface, struct sockaddr *dest);
	static session *create(int cid, int did, int tid);
	static session *create(call *cr, int cid);
	static void destroy(session *s);
	static void destroy(call *cr);
	static void disjoin(call *cr);
	static void detach(session *s);
	static void clear(session *s);
	static void close(session *s);
	static session *access(int cid);
	static char *sipHostid(const char *uri, char *buf, size_t size);
	static char *sipUserid(const char *uri, char *buf, size_t size);
	static char *sipAddress(struct sockaddr_internet *addr, char *buf, const char *user = NULL, size_t size = MAX_URI_SIZE);
	static char *sipPublish(struct sockaddr_internet *addr, char *buf, const char *user = NULL, size_t size = MAX_URI_SIZE);
	static char *sipIdentity(struct sockaddr_internet *addr, char *buf, const char *user = NULL, size_t size = MAX_IDENT_SIZE);
	static char *sipContact(struct sockaddr_internet *addr, char *buf, const char *user = NULL, const char *display = NULL, size_t size = MAX_URI_SIZE);
	static Socket::address *getAddress(const char *uri, Socket::address *addr = NULL);
	static void siplog(osip_message_t *msg);
	static void enableDumping(void);
	static void infomsg(session *session, eXosip_event_t *sevent);
	static void setDialog(session *session, int did);
	static int getDialog(session *session);

	inline static timeout_t ringTimeout(void)
		{return stack::sip.ring_timer;};

	inline static timeout_t cfnaTimeout(void)
		{return stack::sip.cfna_timer;};

	inline static timeout_t resetTimeout(void)
		{return stack::sip.reset_timer;};

	inline static unsigned inviteExpires(void)
		{return stack::sip.invite_expires;};
};

class __LOCAL server : public service
{
private:
	typedef	linked_value<profile_t, LinkedObject> profile;

	class __LOCAL keymap : public LinkedObject
	{
	public:
		service::keynode *node;
		const char *id;
	};

	LinkedObject *keys[CONFIG_KEY_SIZE];
	cidr::policy *acl;
	keynode **extmap;
	keynode *provision;
	LinkedObject *profiles;

	bool create(const char *id, keynode *node);
	keynode *find(const char *id);

	bool confirm(const char *user);
	void dump(FILE *fp);

public:
	server(const char *id);

	static bool flags_gateway;
	static bool check(void);
	static profile_t *getProfile(const char *id); 
	static keynode *getRouting(const char *id);
	static keynode *getProvision(const char *id);
	static keynode *getExtension(const char *id);
	static keynode *getConfig(void);
	static unsigned getForwarding(const char *id);
	static cidr *getPolicy(struct sockaddr *addr);
	static bool isLocal(struct sockaddr *addr);
	static void release(cidr *access);
	static void release(keynode *node);
	static void reload(const char *uid);
	static void utils(const char *uid);
	static Socket::address *getContact(const char *id);
	static unsigned forwarding(keynode *node);
	static void plugins(const char *argv0, const char *names);
	static void usage(void);
	static void version(void);
	static void run(const char *user);
	static void stop(void);
	static caddr_t allocate(size_t size, LinkedObject **list, volatile unsigned *count = NULL);
	static unsigned allocate(void);
	static void regdump(void);	
};

class __LOCAL messages : public service::callback
{
private:
	class __LOCAL message : public LinkedObject
	{
	public:
		time_t expires;
		char user[MAX_USERID_SIZE];
		char type[64];
		char from[MAX_URI_SIZE];
		char body[1024];
		char reply[MAX_USERID_SIZE];
		int msglen;
		void create();
	};

	static messages manager;

	bool check(void);
	bool reload(service *cfg);
	void cleanup(void);
	void snapshot(FILE *fp);

	static bool deliver(message *msg);

public:
	messages();

	static void automatic(void);
	static void update(const char *userid);
	static bool publish(const char *to, const char *reply, const char *from, caddr_t body, size_t size, const char *msgtype);
	static bool system(const char *to, const char *message);
};


class __LOCAL thread : private DetachedThread
{
private:
	friend class stack;
	friend class stack::call;

	unsigned instance;
	unsigned extension;
	cidr *access;
	service::keynode *authorized;
	service::keynode *dialed;
	registry::mapped *reginfo;
	eXosip_event_t *sevent;
	char buffer[MAX_URI_SIZE];	
	char identbuf[MAX_USERID_SIZE + 12];
	char identity[MAX_USERID_SIZE];
	char dialing[MAX_USERID_SIZE];
	char display[MAX_DISPLAY_SIZE];
	char requesting[MAX_URI_SIZE];
	struct sockaddr_internet iface;
	Socket::address via_address, from_address, request_address;
	stack::session *session;
	osip_header_t *header;
	long header_expires;
	osip_via_t *via_header;
	osip_from_t *from;
	osip_to_t *to;
	osip_uri_t *uri;
	const char *via_host;
	unsigned via_hops;
	unsigned via_port;
	stack::session proxyinfo;

	enum {EXTERNAL, LOCAL, PUBLIC, ROUTED, FORWARDED} destination;
	enum {CALL, MESSAGE, REGISTRAR, NONE} authorizing;

	thread();

	static void wait(unsigned count);

	void send_reply(int error);
	void expiration(void);
	void inviteRemote(stack::session *session, const char *uri);
	void inviteLocal(stack::session *session, registry::mapped *rr);
	void invite(void);
	void identify(void);
	bool getsource(void);
	bool unauthenticated(void);
	bool authenticate(void);
	bool authorize(void);
	void registration(void);
	void validate(void);
	void message(void);
	void reregister(const char *contact, time_t interval);
	void deregister(void);
	void challenge(void);
	void options(void);
	void run(void);
	void getDevice(registry::mapped *rr);
	const char *getIdent(void);

public:
	static void shutdown(void);
};

class __LOCAL proxy : private service::callback
{
private:
	unsigned short port;
	unsigned count;
	volatile char *published;
	cidr::policy *nets;

	static proxy rtp;

	class __LOCAL listener : public JoinableThread
	{
	public:
		listener();

	private:
		void run(void);
	};

	listener *thr;

	friend class proxy::listener;

public:
	proxy();

	static bool assign(stack::call *cr, unsigned count);
	static bool classify(stack::session *session, stack::session *source, struct sockaddr *addr);
	static void copy(stack::session *target, stack::session *source);
	static bool isRequired(void);

	bool publishingAddress(const char *address);
	bool reload(service *cfg);
	void start(service *cfg);
	void stop(service *cfg);
	void snapshot(FILE *fp);
};

END_NAMESPACE

