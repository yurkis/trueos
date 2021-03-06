/*-
 * Copyright (c) 2017 Rick Macklem
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <err.h>
#include <getopt.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/extattr.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fs/nfs/nfskpiport.h>
#include <fs/nfs/nfsproto.h>
#include <fs/nfs/nfs.h>
#include <fs/nfs/nfsrvstate.h>

static void usage(void);

static struct option longopts[] = {
	{ "changeds",	required_argument,	NULL,	'c'	},
	{ "quiet",	no_argument,		NULL,	'q'	},
	{ "ds",		required_argument,	NULL,	's'	},
	{ "zerofh",	no_argument,		NULL,	'z'	},
	{ NULL,		0,			NULL,	0	}
};

/*
 * This program displays the location information of a data storage file
 * for a given file on a MetaData Server (MDS) in a pNFS service.  This program
 * must be run on the MDS and the file argument must be a file in a local
 * file system that has been exported for the pNFS service.
 */
int
main(int argc, char *argv[])
{
	struct addrinfo *res, *ad, *newres;
	struct sockaddr_in *sin, *adsin;
	struct sockaddr_in6 *sin6, *adsin6;
	char hostn[2 * NI_MAXHOST + 2], *cp;
	struct pnfsdsfile dsfile[NFSDEV_MAXMIRRORS];
	int ch, dosetxattr, i, mirrorcnt, quiet, zerofh;
	in_port_t tport;
	ssize_t xattrsize;

	zerofh = 0;
	quiet = 0;
	dosetxattr = 0;
	res = NULL;
	newres = NULL;
	while ((ch = getopt_long(argc, argv, "c:qs:z", longopts, NULL)) != -1) {
		switch (ch) {
		case 'c':
			/* Replace the first DS server with the second one. */
			if (zerofh != 0)
				errx(1, "-c and -z are mutually exclusive\n");
			strlcpy(hostn, optarg, 2 * NI_MAXHOST + 2);
			cp = strchr(hostn, ',');
			if (cp == NULL)
				errx(1, "Bad -c argument %s\n", hostn);
			*cp = '\0';
			if (getaddrinfo(hostn, NULL, NULL, &res) != 0)
				errx(1, "Can't get IP# for %s\n", hostn);
			*cp++ = ',';
			if (getaddrinfo(cp, NULL, NULL, &newres) != 0)
				errx(1, "Can't get IP# for %s\n", cp);
			break;
		case 'q':
			quiet = 1;
			break;
		case 's':
			/* Translate the server name to an IP address. */
			if (getaddrinfo(optarg, NULL, NULL, &res) != 0)
				errx(1, "Can't get IP# for %s\n", optarg);
			break;
		case 'z':
			if (newres != NULL)
				errx(1, "-c and -z are mutually exclusive\n");
			zerofh = 1;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	if (argc != 1)
		usage();
	argv += optind;

	/*
	 * The host address and directory where the data storage file is
	 * located is in the extended attribute "pnfsd.dsfile".
	 */
	xattrsize = extattr_get_file(*argv, EXTATTR_NAMESPACE_SYSTEM,
	    "pnfsd.dsfile", dsfile, sizeof(dsfile));
	mirrorcnt = xattrsize / sizeof(struct pnfsdsfile);
	if (mirrorcnt < 1 || xattrsize != mirrorcnt * sizeof(struct pnfsdsfile))
		err(1, "Can't get extattr pnfsd.dsfile\n");

	for (i = 0; i < mirrorcnt; i++) {
		if (i > 0)
			printf(" ");
		/* Do the zerofh option. You must be root. */
		if (zerofh != 0) {
			if (geteuid() != 0)
				errx(1, "Must be root/su to zerofh\n");
	
			/*
			 * Do it for the server specified by -s/--ds or all
			 * servers, if -s/--ds was not specified.
			 */
			sin = &dsfile[i].dsf_sin;
			sin6 = &dsfile[i].dsf_sin6;
			ad = res;
			while (ad != NULL) {
				adsin = (struct sockaddr_in *)ad->ai_addr;
				adsin6 = (struct sockaddr_in6 *)ad->ai_addr;
				if (adsin->sin_family == sin->sin_family) {
					if (sin->sin_family == AF_INET &&
					    sin->sin_addr.s_addr ==
					    adsin->sin_addr.s_addr)
						break;
					else if (sin->sin_family == AF_INET6 &&
					    IN6_ARE_ADDR_EQUAL(&sin6->sin6_addr,
					    &adsin6->sin6_addr))
						break;
				}
				ad = ad->ai_next;
			}
			if (res == NULL || ad != NULL) {
				memset(&dsfile[i].dsf_fh, 0, sizeof(fhandle_t));
				dosetxattr = 1;
			}
		}
	
		/* Do the -c option to replace the DS host address. */
		if (newres != NULL) {
			if (geteuid() != 0)
				errx(1, "Must be root/su to replace the host"
				    " addr\n");
	
			/*
			 * Check that the old host address matches.
			 */
			sin = &dsfile[i].dsf_sin;
			sin6 = &dsfile[i].dsf_sin6;
			ad = res;
			while (ad != NULL) {
				adsin = (struct sockaddr_in *)ad->ai_addr;
				adsin6 = (struct sockaddr_in6 *)ad->ai_addr;
				if (adsin->sin_family == sin->sin_family) {
					if (sin->sin_family == AF_INET &&
					    sin->sin_addr.s_addr ==
					    adsin->sin_addr.s_addr)
						break;
					else if (sin->sin_family == AF_INET6 &&
					    IN6_ARE_ADDR_EQUAL(&sin6->sin6_addr,
					    &adsin6->sin6_addr))
						break;
				}
				ad = ad->ai_next;
			}
			if (ad != NULL) {
				if (sin->sin_family == AF_INET)
					tport = sin->sin_port;
				else
					tport = sin6->sin6_port;
				/*
				 * We have a match, so replace it with the first
				 * AF_INET or AF_INET6 address in the newres
				 * list.
				 */
				while (newres->ai_addr->sa_family != AF_INET &&
				    newres->ai_addr->sa_family != AF_INET6) {
					newres = newres->ai_next;
					if (newres == NULL)
						errx(1, "Hostname %s has no"
						    " IP#\n", cp);
				}
				if (newres->ai_addr->sa_family == AF_INET) {
					memcpy(sin, newres->ai_addr,
					    sizeof(*sin));
					sin->sin_port = tport;
				} else if (newres->ai_addr->sa_family ==
				    AF_INET6) {
					memcpy(sin6, newres->ai_addr,
					    sizeof(*sin6));
					sin6->sin6_port = tport;
				}
				dosetxattr = 1;
			}
		}
	
		if (quiet == 0) {
			/* Translate the IP address to a hostname. */
			if (getnameinfo((struct sockaddr *)&dsfile[i].dsf_sin,
			    dsfile[i].dsf_sin.sin_len, hostn, sizeof(hostn),
			    NULL, 0, 0) < 0)
				err(1, "Can't get hostname\n");
			printf("%s\tds%d/%s", hostn, dsfile[i].dsf_dir,
			    dsfile[i].dsf_filename);
		}
	}
	if (quiet == 0)
		printf("\n");
	if (dosetxattr != 0 && extattr_set_file(*argv, EXTATTR_NAMESPACE_SYSTEM,
	    "pnfsd.dsfile", dsfile, xattrsize) != xattrsize)
		err(1, "Can't set pnfsd.dsfile\n");
}

static void
usage(void)
{

	fprintf(stderr, "pnfsdsfile [-q/--quiet] [-z/--zerofh] "
	    "[-s/--ds <dshostname>] <filename>\n");
	exit(1);
}

