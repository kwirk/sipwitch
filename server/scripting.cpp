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
#include <config.h>

NAMESPACE_SIPWITCH
using namespace UCOMMON_NAMESPACE;

static const char *dirpath = NULL;

class __LOCAL scripting : public service::callback
{
public:
	scripting();

private:
	void start(service *cfg);
	bool reload(service *cfg);
	void activating(MappedRegistry *rr);
	void expiring(MappedRegistry *rr);
};

static scripting scripting_plugin;

scripting::scripting() :
service::callback(0)
{
	process::errlog(INFO, "scripting plugin loaded");
}

bool scripting::reload(service *cfg)
{
	assert(cfg != NULL);

	if(dirpath == NULL)
		start(cfg);
	
	return true;
}

void scripting::start(service *cfg)
{
	assert(cfg != NULL);
	
	static char buf[256];
	service::keynode *env = cfg->getPath("environ");
	const char *home = service::getValue(env, "HOME");

	if(fsys::isdir(DEFAULT_CFGPATH "/sysconfig/sipwitch-scripts"))
		dirpath = DEFAULT_CFGPATH "/sysconfig/sipwitch-scripts";
	else if(fsys::isdir(DEFAULT_LIBEXEC "/sipwitch"))
		dirpath = DEFAULT_LIBEXEC "/sipwitch";
	else if(home) {
        snprintf(buf, sizeof(buf), "%s/.sipwitch-scripts", home);
		if(fsys::isdir(buf))
			dirpath = buf;
	}

	if(dirpath)
		process::errlog(INFO, "scripting plugin path %s", dirpath);
	else
		process::errlog(ERRLOG, "scripting plugin disabled; no script directory");
}

void scripting::activating(MappedRegistry *rr)
{
	char addr[128];
	if(!dirpath)
		return;

	Socket::getaddress((struct sockaddr *)&rr->contact, addr, sizeof(addr));
	process::system("%s/sipup %s %d %s:%d %d", dirpath, rr->userid, rr->ext, 
		addr, Socket::getservice((struct sockaddr *)&rr->contact), 
		(int)(rr->type - MappedRegistry::EXPIRED));
}

void scripting::expiring(MappedRegistry *rr)
{
	process::system("%s/sipdown %s %d", dirpath, rr->userid, rr->ext);
}

END_NAMESPACE
