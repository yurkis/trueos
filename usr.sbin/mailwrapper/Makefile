# $FreeBSD$

.include <src.opts.mk>

.if ${MK_MAILWRAPPER} != "no"
PROG=	mailwrapper
MAN=	mailwrapper.8

LIBADD=	util
.endif

.if ${MK_MAILWRAPPER} != "no" || ${MK_SENDMAIL} != "no"
SYMLINKS=	../sbin/mailwrapper /usr/bin/mailq \
		../sbin/mailwrapper /usr/bin/newaliases \
		mailwrapper /usr/sbin/hoststat \
		mailwrapper /usr/sbin/purgestat \
		mailwrapper /usr/sbin/sendmail

.if ${MK_MAILWRAPPER} == "no" && ${MK_SENDMAIL} != "no"
SYMLINKS+=	../libexec/sendmail/sendmail ${BINDIR}/mailwrapper
.endif
.endif

.if ${MK_MAILWRAPPER} != "no" && ${MK_SENDMAIL} == "no"
SYMLINKS+=	..${BINDIR}/mailwrapper /bin/rmail
.endif

.if ${MK_MAILWRAPPER} != "no"
.if !exists(${DESTDIR}/etc/mail/mailer.conf)
CONFSS=		${SRCTOP}/usr.sbin/sendmail/mail/mailer.conf
CONFDIR=	/etc/mail
CONFMODE=	644
.endif
.endif

.include <bsd.prog.mk>
