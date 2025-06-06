/*
 * Dropbear - a SSH2 server
 * 
 * Copyright (c) 2002,2003 Matt Johnston
 * All rights reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE. */

#include "includes.h"
#include "session.h"
#include "dbutil.h"
#include "packet.h"
#include "algo.h"
#include "buffer.h"
#include "dss.h"
#include "ssh.h"
#include "dbrandom.h"
#include "kex.h"
#include "channel.h"
#include "chansession.h"
#include "atomicio.h"
#include "tcpfwd.h"
#include "service.h"
#include "auth.h"
#include "runopts.h"
#include "crypto_desc.h"
#include "fuzz.h"

static void svr_remoteclosed(void);
static void svr_algos_initialise(void);

struct serversession svr_ses; /* GLOBAL */

static const packettype svr_packettypes[] = {
	{SSH_MSG_CHANNEL_DATA, recv_msg_channel_data},
	{SSH_MSG_CHANNEL_WINDOW_ADJUST, recv_msg_channel_window_adjust},
	{SSH_MSG_USERAUTH_REQUEST, recv_msg_userauth_request}, /* server */
	{SSH_MSG_SERVICE_REQUEST, recv_msg_service_request}, /* server */
	{SSH_MSG_KEXINIT, recv_msg_kexinit},
	{SSH_MSG_KEXDH_INIT, recv_msg_kexdh_init}, /* server */
	{SSH_MSG_NEWKEYS, recv_msg_newkeys},
	{SSH_MSG_GLOBAL_REQUEST, recv_msg_global_request_remotetcp},
	{SSH_MSG_CHANNEL_REQUEST, recv_msg_channel_request},
	{SSH_MSG_CHANNEL_OPEN, recv_msg_channel_open},
	{SSH_MSG_CHANNEL_EOF, recv_msg_channel_eof},
	{SSH_MSG_CHANNEL_CLOSE, recv_msg_channel_close},
	{SSH_MSG_CHANNEL_SUCCESS, ignore_recv_response},
	{SSH_MSG_CHANNEL_FAILURE, ignore_recv_response},
	{SSH_MSG_REQUEST_FAILURE, ignore_recv_response}, /* for keepalive */
	{SSH_MSG_REQUEST_SUCCESS, ignore_recv_response}, /* client */
#if DROPBEAR_LISTENERS
	{SSH_MSG_CHANNEL_OPEN_CONFIRMATION, recv_msg_channel_open_confirmation},
	{SSH_MSG_CHANNEL_OPEN_FAILURE, recv_msg_channel_open_failure},
#endif
	{0, NULL} /* End */
};

static const struct ChanType *svr_chantypes[] = {
	&svrchansess,
#if DROPBEAR_SVR_LOCALTCPFWD
	&svr_chan_tcpdirect,
#endif
	NULL /* Null termination is mandatory. */
};

static void
svr_session_cleanup(void) {
	/* free potential public key options */
	svr_pubkey_options_cleanup();

	m_free(svr_ses.addrstring);
#ifdef SECURITY_NOTIFY
	m_free(svr_ses.hoststring);
#endif
	m_free(svr_ses.remotehost);
	m_free(svr_ses.childpids);
	svr_ses.childpidsize = 0;

#if DROPBEAR_PLUGIN
        if (svr_ses.plugin_handle != NULL) {
            if (svr_ses.plugin_instance) {
                svr_ses.plugin_instance->delete_plugin(svr_ses.plugin_instance);
                svr_ses.plugin_instance = NULL;
            }

            dlclose(svr_ses.plugin_handle);
            svr_ses.plugin_handle = NULL;
        }
#endif
}

void svr_session(int sock, int childpipe) {
	char *host, *port;
	size_t len;

	common_session_init(sock, sock);

	/* Initialise server specific parts of the session */
	svr_ses.childpipe = childpipe;
#if DROPBEAR_VFORK
	svr_ses.server_pid = getpid();
#endif

	/* for logging the remote address */
	get_socket_address(ses.sock_in, NULL, NULL, &host, &port, 0);
	len = strlen(host) + strlen(port) + 2;
	svr_ses.addrstring = m_malloc(len);
	snprintf(svr_ses.addrstring, len, "%s:%s", host, port);
#ifdef SECURITY_NOTIFY
	svr_ses.hoststring = host;
#else
	m_free(host);
#endif
	m_free(port);

#if DROPBEAR_PLUGIN
        /* Initializes the PLUGIN Plugin */
        svr_ses.plugin_handle = NULL;
        svr_ses.plugin_instance = NULL;
        if (svr_opts.pubkey_plugin) {
#if DEBUG_TRACE
            const int verbose = debug_trace;
#else
            const int verbose = 0;
#endif
            PubkeyExtPlugin_newFn  pluginConstructor;

            /* RTLD_NOW: fails if not all the symbols are resolved now. Better fail now than at run-time */
            svr_ses.plugin_handle = dlopen(svr_opts.pubkey_plugin, RTLD_NOW);
            if (svr_ses.plugin_handle == NULL) {
                dropbear_exit("failed to load external pubkey plugin '%s': %s", svr_opts.pubkey_plugin, dlerror());
            }
            pluginConstructor = (PubkeyExtPlugin_newFn)dlsym(svr_ses.plugin_handle, DROPBEAR_PUBKEY_PLUGIN_FNNAME_NEW);
            if (!pluginConstructor) {
                dropbear_exit("plugin constructor method not found in external pubkey plugin");
            }

            /* Create an instance of the plugin */
            svr_ses.plugin_instance = pluginConstructor(verbose, svr_opts.pubkey_plugin_options, svr_ses.addrstring);
            if (svr_ses.plugin_instance == NULL) {
                dropbear_exit("external plugin initialization failed");
            }
            /* Check if the plugin is compatible */
            if ( (svr_ses.plugin_instance->api_version[0] != DROPBEAR_PLUGIN_VERSION_MAJOR) ||
                 (svr_ses.plugin_instance->api_version[1] < DROPBEAR_PLUGIN_VERSION_MINOR) ) {
                dropbear_exit("plugin version check failed: "
                              "Dropbear=%d.%d, plugin=%d.%d",
                        DROPBEAR_PLUGIN_VERSION_MAJOR, DROPBEAR_PLUGIN_VERSION_MINOR,
                        svr_ses.plugin_instance->api_version[0], svr_ses.plugin_instance->api_version[1]);
            }
            if (svr_ses.plugin_instance->api_version[1] > DROPBEAR_PLUGIN_VERSION_MINOR) {
                dropbear_log(LOG_WARNING, "plugin API newer than dropbear API: "
                              "Dropbear=%d.%d, plugin=%d.%d",
                        DROPBEAR_PLUGIN_VERSION_MAJOR, DROPBEAR_PLUGIN_VERSION_MINOR,
                        svr_ses.plugin_instance->api_version[0], svr_ses.plugin_instance->api_version[1]);
            }
            dropbear_log(LOG_INFO, "successfully loaded and initialized pubkey plugin '%s'", svr_opts.pubkey_plugin);
        }
#endif

	svr_authinitialise();
	chaninitialise(svr_chantypes);
	svr_chansessinitialise();
	svr_algos_initialise();

	get_socket_address(ses.sock_in, NULL, NULL, 
			&svr_ses.remotehost, NULL, 1);

	/* set up messages etc */
	ses.remoteclosed = svr_remoteclosed;
	ses.extra_session_cleanup = svr_session_cleanup;

	/* packet handlers */
	ses.packettypes = svr_packettypes;

	ses.isserver = 1;

	/* We're ready to go now */
	ses.init_done = 1;

	/* exchange identification, version etc */
	send_session_identification();
	
	kexfirstinitialise(); /* initialise the kex state */

	/* start off with key exchange */
	send_msg_kexinit();

	/* Run the main for loop. NULL is for the dispatcher - only the client
	 * code makes use of it */
	session_loop(svr_chansess_checksignal);

	/* Not reached */

}

/* failure exit - format must be <= 100 chars */
void svr_dropbear_exit(int exitcode, const char* format, va_list param) {
	char exitmsg[150];
	char fullmsg[300];
	char fromaddr[60];
	int i;

#if DROPBEAR_PLUGIN
        if ((ses.plugin_session != NULL)) {
            svr_ses.plugin_instance->delete_session(ses.plugin_session);
        }
        ses.plugin_session = NULL;
#endif

	/* Render the formatted exit message */
	vsnprintf(exitmsg, sizeof(exitmsg), format, param);

	/* svr_ses.addrstring may not be set for some early exits, or for
	the listener process */
	fromaddr[0] = '\0';
	if (svr_ses.addrstring) {
	    snprintf(fromaddr, sizeof(fromaddr), " from <%s>", svr_ses.addrstring);
    }

	/* Add the prefix depending on session/auth state */
	if (!ses.init_done) {
		/* before session init */
		snprintf(fullmsg, sizeof(fullmsg), "Early exit%s: %s", fromaddr, exitmsg);
	} else if (ses.authstate.authdone) {
		/* user has authenticated */
		snprintf(fullmsg, sizeof(fullmsg),
				"Exit (%s)%s: %s", 
				ses.authstate.pw_name, fromaddr, exitmsg);
	} else if (ses.authstate.pw_name) {
		/* we have a potential user */
		snprintf(fullmsg, sizeof(fullmsg), 
				"Exit before auth%s: (user '%s', %u fails): %s",
				fromaddr, ses.authstate.pw_name, ses.authstate.failcount, exitmsg);
	} else {
		/* before userauth */
		snprintf(fullmsg, sizeof(fullmsg), "Exit before auth%s: %s", fromaddr, exitmsg);
	}

	dropbear_log(LOG_INFO, "%s", fullmsg);

#if DROPBEAR_VFORK
	/* For uclinux only the main server process should cleanup - we don't want
	 * forked children doing that */
	if (svr_ses.server_pid == getpid())
#endif
	{
		/* must be after we've done with username etc */
		session_cleanup();
	}

#if DROPBEAR_FUZZ
	/* longjmp before cleaning up svr_opts */
    if (fuzz.do_jmp) {
        longjmp(fuzz.jmp, 1);
    }
#endif

	if (svr_opts.hostkey) {
		sign_key_free(svr_opts.hostkey);
		svr_opts.hostkey = NULL;
	}
	for (i = 0; i < DROPBEAR_MAX_PORTS; i++) {
		m_free(svr_opts.addresses[i]);
		m_free(svr_opts.ports[i]);
	}

    
	exit(exitcode);

}

/* priority is priority as with syslog() */
void svr_dropbear_log(int priority, const char* format, va_list param) {

	char printbuf[1024];
	char datestr[20];
	time_t timesec;
	int havetrace = 0;

	vsnprintf(printbuf, sizeof(printbuf), format, param);

#ifndef DISABLE_SYSLOG
	if (opts.usingsyslog) {
		syslog(priority, "%s", printbuf);
	}
#endif

	/* if we are using DEBUG_TRACE, we want to print to stderr even if
	 * syslog is used, so it is included in error reports */
#if DEBUG_TRACE
	havetrace = debug_trace;
#endif

	if (!opts.usingsyslog || havetrace) {
		struct tm * local_tm = NULL;
		timesec = time(NULL);
		local_tm = localtime(&timesec);
		if (local_tm == NULL
			|| strftime(datestr, sizeof(datestr), "%b %d %H:%M:%S", 
						local_tm) == 0)
		{
			/* upon failure, just print the epoch-seconds time. */
			snprintf(datestr, sizeof(datestr), "%d", (int)timesec);
		}
		fprintf(stderr, "[%d] %s %s\n", getpid(), datestr, printbuf);
	}
}

/* called when the remote side closes the connection */
static void svr_remoteclosed() {

	m_close(ses.sock_in);
	if (ses.sock_in != ses.sock_out) {
		m_close(ses.sock_out);
	}
	ses.sock_in = -1;
	ses.sock_out = -1;
	dropbear_close("Exited normally");

}

static void svr_algos_initialise(void) {
	algo_type *algo;
	for (algo = sshkex; algo->name; algo++) {
#if DROPBEAR_DH_GROUP1 && DROPBEAR_DH_GROUP1_CLIENTONLY
		if (strcmp(algo->name, "diffie-hellman-group1-sha1") == 0) {
			algo->usable = 0;
		}
#endif
#if DROPBEAR_EXT_INFO
		if (strcmp(algo->name, SSH_EXT_INFO_C) == 0) {
			algo->usable = 0;
		}
#endif
		if (strcmp(algo->name, SSH_STRICT_KEX_C) == 0) {
			algo->usable = 0;
		}
	}
}

