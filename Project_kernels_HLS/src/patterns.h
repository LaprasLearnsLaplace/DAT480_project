#ifndef PATTERNS_H
#define PATTERNS_H

#include <ap_int.h>

#define NUM_PATTERNS 256
#define PATTERN_MAX_LEN 32

typedef struct {
    unsigned char data[PATTERN_MAX_LEN];
    unsigned char tap_idx[PATTERN_MAX_LEN];
    int len;
} Pattern;

constexpr Pattern make_pattern(const char *lit) {
    Pattern p = { {0}, {0}, 0 };
    int len = 0;
    while (len < PATTERN_MAX_LEN && lit[len] != '\0') {
        p.data[len] = static_cast<unsigned char>(lit[len]);
        ++len;
    }
    p.len = len;
    for (int i = 0; i < len; ++i) {
        p.tap_idx[i] = static_cast<unsigned char>(len - 1 - i);
    }
    return p;
}

static const Pattern rules[NUM_PATTERNS] = {
    make_pattern("/bnbform.cgi"),  // 1: /bnbform.cgi
    make_pattern("/bb/index.php"),  // 2: /bb/index.php
    make_pattern("/server-status"),  // 3: /server-status
    make_pattern("/nph-exploitscanget.cgi"),  // 4: /nph-exploitscanget.cgi
    make_pattern("CAL "),  // 5: CAL 
    make_pattern("\x0b"),  // 6: |0B|
    make_pattern(".html?0."),  // 7: .html?0.
    make_pattern("/android/sms/sync.php"),  // 8: /android/sms/sync.php
    make_pattern("/info2www"),  // 9: /info2www
    make_pattern("/changepw.exe"),  // 10: /changepw.exe
    make_pattern("/purchase.php?a="),  // 11: /purchase.php?a=
    make_pattern(".rp"),  // 12: .rp
    make_pattern("\x05)\x00\x00\x00"),  // 13: |05 29 00 00 00|
    make_pattern("/?u="),  // 14: /?u=
    make_pattern("Subject: =?utf-8?B?"),  // 15: Subject|3A 20|=?utf-8?B?
    make_pattern("/global.cgi"),  // 16: /global.cgi
    make_pattern("Hello..."),  // 17: Hello...
    make_pattern("/input.bat"),  // 18: /input.bat
    make_pattern("/GWWEB.EXE?"),  // 19: /GWWEB.EXE?
    make_pattern("/.history"),  // 20: /.history
    make_pattern("/_vti_bin/_vti_aut/author.exe"),  // 21: /_vti_bin/_vti_aut/author.exe
    make_pattern("/emumail.cgi"),  // 22: /emumail.cgi
    make_pattern("/instapi.php?idMk="),  // 23: /instapi.php?idMk=
    make_pattern("dbms_repcat.add_priority_nchar"),  // 24: dbms_repcat.add_priority_nchar
    make_pattern("/0/ HTTP/1."),  // 25: /0/ HTTP/1.
    make_pattern("ADMINISTRATOR"),  // 26: ADMINISTRATOR
    make_pattern("TERM=xterm"),  // 27: TERM=xterm
    make_pattern("/msdac/"),  // 28: /msdac/
    make_pattern("?PageServices"),  // 29: ?PageServices
    make_pattern("User-Agent: Mindspark MIP "),  // 30: User-Agent|3A 20|Mindspark MIP 
    make_pattern("Host: mbfce24rgn65bx3g."),  // 31: Host: mbfce24rgn65bx3g.
    make_pattern("\x18\x03\x03"),  // 32: |18 03 03|
    make_pattern("/js/disable.js?type="),  // 33: /js/disable.js?type=
    make_pattern("Frag"),  // 34: Frag
    make_pattern("\x00\x00\x00\x11\xd0\x00\x00\x00"),  // 35: |00 00 00 11 D0 00 00 00|
    make_pattern("1 file(s) copied"),  // 36: 1 file|28|s|29| copied
    make_pattern("00"),  // 37: 00
    make_pattern("/search97.vts"),  // 38: /search97.vts
    make_pattern("POST /"),  // 39: POST /
    make_pattern("/scripts/Fpadmcgi.exe"),  // 40: /scripts/Fpadmcgi.exe
    make_pattern("/onrequestend.cfm"),  // 41: /onrequestend.cfm
    make_pattern("%APPDATA%"),  // 42: %APPDATA%
    make_pattern("/post/echo"),  // 43: /post/echo
    make_pattern("Cookie: cache=cc2="),  // 44: Cookie: cache=cc2=
    make_pattern("TIME_ZONE"),  // 45: TIME_ZONE
    make_pattern("/form2raw.cgi"),  // 46: /form2raw.cgi
    make_pattern("RNFR "),  // 47: RNFR 
    make_pattern(".exe HTTP/1.0\x0d\x0aHost:"),  // 48: .exe HTTP/1.0|0D 0A|Host:
    make_pattern("/b/index.php?id="),  // 49: /b/index.php?id=
    make_pattern("/echo.bat"),  // 50: /echo.bat
    make_pattern("/CSMailto.cgi"),  // 51: /CSMailto.cgi
    make_pattern("create table"),  // 52: create table
    make_pattern("grant "),  // 53: grant 
    make_pattern("javascript://"),  // 54: javascript|3A|//
    make_pattern("dbms_repcat.alter_priority"),  // 55: dbms_repcat.alter_priority
    make_pattern("tcpdata|"),  // 56: tcpdata|7C|
    make_pattern("/prok/"),  // 57: /prok/
    make_pattern("= HTTP/1."),  // 58: = HTTP/1.
    make_pattern("/input.bat|"),  // 59: /input.bat|7C|
    make_pattern("PK"),  // 60: PK
    make_pattern("<iframe"),  // 61: <iframe
    make_pattern("User-Agent: WinHttpClient"),  // 62: User-Agent|3A| WinHttpClient
    make_pattern("Connect.php?id="),  // 63: Connect.php?id=
    make_pattern("\xff\x01\x00\x00\x00\x00\x01"),  // 64: |FF 01 00 00 00 00 01|
    make_pattern(".swf?"),  // 65: .swf?
    make_pattern("\x03\x00\x01"),  // 66: |03 00 01|
    make_pattern("CCCCCCCCCCCCCCCCCCCCCCCC"),  // 67: CCCCCCCCCCCCCCCCCCCCCCCC
    make_pattern("sleep|"),  // 68: sleep|7C|
    make_pattern("/register.cgi"),  // 69: /register.cgi
    make_pattern("pong"),  // 70: pong
    make_pattern("/calender_admin.pl"),  // 71: /calender_admin.pl
    make_pattern("user_tablespace"),  // 72: user_tablespace
    make_pattern(".php?method="),  // 73: .php?method=
    make_pattern("\xfeSMB@\x00"),  // 74: |FE|SMB|40 00|
    make_pattern("\xffSMBs"),  // 75: |FF|SMBs
    make_pattern("/orders/checks.txt"),  // 76: /orders/checks.txt
    make_pattern("\x16\x03\x00"),  // 77: |16 03 00|
    make_pattern("/catalog.nsf"),  // 78: /catalog.nsf
    make_pattern("StoogR"),  // 79: StoogR
    make_pattern("/cfcache.map"),  // 80: /cfcache.map
    make_pattern("/inst?"),  // 81: /inst?
    make_pattern("/directory.php"),  // 82: /directory.php
    make_pattern("/hi.cgi"),  // 83: /hi.cgi
    make_pattern("/lockycrypt.rar"),  // 84: /lockycrypt.rar
    make_pattern("/shop.cgi"),  // 85: /shop.cgi
    make_pattern("/web/google_analytics.php"),  // 86: /web/google_analytics.php
    make_pattern("Acunetix-"),  // 87: Acunetix-
    make_pattern("Host: 209.53.113.223\x0d\x0a"),  // 88: Host|3A| 209.53.113.223|0D 0A|
    make_pattern("\x17\x03\x03"),  // 89: |17 03 03|
    make_pattern("spoofworks"),  // 90: spoofworks
    make_pattern("/axs.cgi"),  // 91: /axs.cgi
    make_pattern("/...."),  // 92: /....
    make_pattern("Command completed"),  // 93: Command completed
    make_pattern("221 Goodbye happy r00ting"),  // 94: 221 Goodbye happy r00ting
    make_pattern("/dvwssr.dll"),  // 95: /dvwssr.dll
    make_pattern("/guestbook.cgi"),  // 96: /guestbook.cgi
    make_pattern("/maillist.pl"),  // 97: /maillist.pl
    make_pattern("/vncviewer.jar"),  // 98: /vncviewer.jar
    make_pattern("/bb-hostsvc.sh?"),  // 99: /bb-hostsvc.sh?
    make_pattern("RCPT TO:"),  // 100: RCPT TO|3A|
    make_pattern("rcpt to:"),  // 101: rcpt to|3A|
    make_pattern("/environ.pl"),  // 102: /environ.pl
    make_pattern("pdf_efax_"),  // 103: pdf_efax_
    make_pattern("/YaBB"),  // 104: /YaBB
    make_pattern("activate"),  // 105: activate
    make_pattern("/000.jpg"),  // 106: /000.jpg
    make_pattern("UPDATE|"),  // 107: UPDATE|7C|
    make_pattern("/a1stats/"),  // 108: /a1stats/
    make_pattern("/VsSetCookie.exe"),  // 109: /VsSetCookie.exe
    make_pattern("/MsmMask.exe"),  // 110: /MsmMask.exe
    make_pattern("/thinner/thumb?img="),  // 111: /thinner/thumb?img=
    make_pattern("/upload/module"),  // 112: /upload/module
    make_pattern("z\x8d\x9b\xdc"),  // 113: |7A 8D 9B DC|
    make_pattern("/cfdocs/snippets/fileexists.cfm"),  // 114: /cfdocs/snippets/fileexists.cfm
    make_pattern("_PHPLIB[libdir]"),  // 115: _PHPLIB[libdir]
    make_pattern("/commerce.cgi"),  // 116: /commerce.cgi
    make_pattern("/Adminhtml_"),  // 117: /Adminhtml_
    make_pattern("User-Agent:"),  // 118: User-Agent|3A|
    make_pattern("/dnstools.php"),  // 119: /dnstools.php
    make_pattern("/windows/update/search?hl="),  // 120: /windows/update/search?hl=
    make_pattern("/finger"),  // 121: /finger
    make_pattern("/cgi-bin/jj"),  // 122: /cgi-bin/jj
    make_pattern("/checkupdate"),  // 123: /checkupdate
    make_pattern("/admisapi/fpadmin.htm"),  // 124: /admisapi/fpadmin.htm
    make_pattern("dbms_repcat.define_column_group"),  // 125: dbms_repcat.define_column_group
    make_pattern("install/upgrade.php"),  // 126: install/upgrade.php
    make_pattern("/whereami.cgi?"),  // 127: /whereami.cgi?
    make_pattern("/backup"),  // 128: /backup
    make_pattern("/pagelog.cgi"),  // 129: /pagelog.cgi
    make_pattern("/mrtg.cgi"),  // 130: /mrtg.cgi
    make_pattern("/frmCompose.aspx"),  // 131: /frmCompose.aspx
    make_pattern("APPE"),  // 132: APPE
    make_pattern("Referer: HTTP/1.0\x0d\x0a"),  // 133: Referer: HTTP/1.0|0D 0A|
    make_pattern("/popup.php"),  // 134: /popup.php
    make_pattern("/admin.php3"),  // 135: /admin.php3
    make_pattern(".pauseAnimations"),  // 136: .pauseAnimations
    make_pattern("/admin_logout.php"),  // 137: /admin_logout.php
    make_pattern("/ul.htm"),  // 138: /ul.htm
    make_pattern("PUT"),  // 139: PUT
    make_pattern("\x08ohtheigh\x02\x63\x63\x00"),  // 140: |08|ohtheigh|02|cc|00|
    make_pattern("\xffSMBs\x00\x00\x00\x00"),  // 141: |FF|SMB|73 00 00 00 00|
    make_pattern("HELP"),  // 142: HELP
    make_pattern("/doeditvotes.cgi"),  // 143: /doeditvotes.cgi
    make_pattern("pass wh00t"),  // 144: pass wh00t
    make_pattern("\x18\x03\x02\x00\x03\x01@\x00"),  // 145: |18 03 02 00 03 01 40 00|
    make_pattern("WashingTon"),  // 146: WashingTon
    make_pattern("/intranet/"),  // 147: /intranet/
    make_pattern("/edit_action.cgi"),  // 148: /edit_action.cgi
    make_pattern("/Sample_showcode.html"),  // 149: /Sample_showcode.html
    make_pattern("WINDIR"),  // 150: WINDIR
    make_pattern("/search.cgi?"),  // 151: /search.cgi?
    make_pattern("/afr.php?zoneid="),  // 152: /afr.php?zoneid=
    make_pattern("sys.dbms_repcat_rq.add_column"),  // 153: sys.dbms_repcat_rq.add_column
    make_pattern("/ping.ashx?action="),  // 154: /ping.ashx?action=
    make_pattern("/GlobalFunctions.php"),  // 155: /GlobalFunctions.php
    make_pattern("/dcforum.cgi"),  // 156: /dcforum.cgi
    make_pattern("&intip="),  // 157: &intip=
    make_pattern("/site/eg/source.asp"),  // 158: /site/eg/source.asp
    make_pattern("newsletter.php"),  // 159: newsletter.php
    make_pattern("login:"),  // 160: login|3A|
    make_pattern("/Config.txt"),  // 161: |2F|Config|2E|txt
    make_pattern("0123456789abcdefghijklmnopqrstuv"),  // 162: 0123456789abcdefghijklmnopqrstuv
    make_pattern("JOIN #biz abc\x0d\x0a"),  // 163: JOIN #biz abc|0D 0A|
    make_pattern("Translate: F"),  // 164: Translate|3A| F
    make_pattern("Wtzup Use"),  // 165: Wtzup Use
    make_pattern(".wmz"),  // 166: .wmz
    make_pattern("dbms_repcat.alter_priority_nchar"),  // 167: dbms_repcat.alter_priority_nchar
    make_pattern("/gcs?alpha="),  // 168: /gcs?alpha=
    make_pattern("FTPON"),  // 169: FTPON
    make_pattern("\x00\x01\x87\x99"),  // 170: |00 01 87 99|
    make_pattern("NLST"),  // 171: NLST
    make_pattern("/cgi-bin/cgi.cgi"),  // 172: /cgi-bin/cgi.cgi
    make_pattern("/shopsearch.asp"),  // 173: /shopsearch.asp
    make_pattern("cat "),  // 174: cat 
    make_pattern("/News/gate.php"),  // 175: /News/gate.php
    make_pattern("/adsamples/config/site.csc"),  // 176: /adsamples/config/site.csc
    make_pattern("ip-who-is.com\x0d\x0a"),  // 177: ip-who-is.com|0D 0A|
    make_pattern("Volume Serial Number"),  // 178: Volume Serial Number
    make_pattern(".bat?"),  // 179: .bat?
    make_pattern("/1/6b-558694705129b01c0"),  // 180: /1/6b-558694705129b01c0
    make_pattern("CMD"),  // 181: CMD
    make_pattern("full|"),  // 182: full|7C|
    make_pattern("/minerd.exe"),  // 183: /minerd.exe
    make_pattern("../"),  // 184: ../
    make_pattern("%SystemRoot%"),  // 185: %SystemRoot%
    make_pattern("550 5.7.1"),  // 186: 550 5.7.1
    make_pattern("/iisadmpwd/aexp2.htr"),  // 187: /iisadmpwd/aexp2.htr
    make_pattern("/CVS/Entries"),  // 188: /CVS/Entries
    make_pattern("/webplus.exe?"),  // 189: /webplus.exe?
    make_pattern("/ion-p"),  // 190: /ion-p
    make_pattern("%COMMONPROGRAMFILES%"),  // 191: %COMMONPROGRAMFILES%
    make_pattern("/insert.inc.php"),  // 192: /insert.inc.php
    make_pattern("dbms_repcat.purge_statistics"),  // 193: dbms_repcat.purge_statistics
    make_pattern("\x17\x03\x00"),  // 194: |17 03 00|
    make_pattern("create"),  // 195: create
    make_pattern("dbms_repcat.set_local_flavor"),  // 196: dbms_repcat.set_local_flavor
    make_pattern("rmgroup"),  // 197: rmgroup
    make_pattern(".cnf"),  // 198: .cnf
    make_pattern("/FtpSaveCVP.dll"),  // 199: /FtpSaveCVP.dll
    make_pattern("/envout.bat|"),  // 200: /envout.bat|7C|
    make_pattern("/ax-admin.cgi"),  // 201: /ax-admin.cgi
    make_pattern("XMKD"),  // 202: XMKD
    make_pattern(".pl"),  // 203: .pl
    make_pattern("alter"),  // 204: alter
    make_pattern("<SNAPQUOTE>"),  // 205: <SNAPQUOTE>
    make_pattern("RENAME"),  // 206: RENAME
    make_pattern("/jsp/snp/"),  // 207: /jsp/snp/
    make_pattern("CF_SETDATASOURCEUSERNAME()"),  // 208: CF_SETDATASOURCEUSERNAME|28 29|
    make_pattern("datapost|"),  // 209: datapost|7C|
    make_pattern("cd.."),  // 210: cd..
    make_pattern("net.exe"),  // 211: net.exe
    make_pattern("password=g00dPa$$w0rD"),  // 212: password=g00dPa$$w0rD
    make_pattern("{\x08**"),  // 213: |7B 08 2A 2A|
    make_pattern("EXECUTE_SYSTEM"),  // 214: EXECUTE_SYSTEM
    make_pattern("/trace.axd"),  // 215: /trace.axd
    make_pattern("/mab.nsf"),  // 216: /mab.nsf
    make_pattern(" .pl"),  // 217:  .pl
    make_pattern("/sdbsearch.cgi"),  // 218: /sdbsearch.cgi
    make_pattern("\x03qov\x02hu\x03\x63om"),  // 219: |03|qov|02|hu|03|com
    make_pattern("/register.dll"),  // 220: /register.dll
    make_pattern("PAT "),  // 221: PAT|20|
    make_pattern("/uploadimage.php"),  // 222: /uploadimage.php
    make_pattern(".php"),  // 223: .php
    make_pattern("%PATHEXT%"),  // 224: %PATHEXT%
    make_pattern("8\x00\x00\x00\xf5\x13\x89S"),  // 225: |38 00 00 00 F5 13 89 53|
    make_pattern("/_admin/"),  // 226: /_admin/
    make_pattern("%USERPROFILE%"),  // 227: %USERPROFILE%
    make_pattern("/_private/orders.htm"),  // 228: /_private/orders.htm
    make_pattern("rotina=plogin&login="),  // 229: rotina=plogin&login=
    make_pattern("/php.cgi"),  // 230: /php.cgi
    make_pattern("APOP"),  // 231: APOP
    make_pattern("/../../"),  // 232: /../../
    make_pattern("/vip.jpg"),  // 233: /vip.jpg
    make_pattern("/new/all_file_info1.php?"),  // 234: /new/all_file_info1.php?
    make_pattern("\x05\x66\x61st8\x07homeftp\x03org\x00"),  // 235: |05|fast8|07|homeftp|03|org|00|
    make_pattern("GET / HTTP/1.1"),  // 236: GET / HTTP/1.1
    make_pattern("/_vti_rpc"),  // 237: /_vti_rpc
    make_pattern("/story.pl"),  // 238: /story.pl
    make_pattern("dbms_repcat.comment_on_repsites"),  // 239: dbms_repcat.comment_on_repsites
    make_pattern(".zollard/"),  // 240: .zollard/
    make_pattern("\xffSMB\xa0"),  // 241: |FF|SMB|A0|
    make_pattern("/cgforum.cgi"),  // 242: /cgforum.cgi
    make_pattern("dbms_repcat.add_grouped_column"),  // 243: dbms_repcat.add_grouped_column
    make_pattern("/admin_password.php"),  // 244: /admin_password.php
    make_pattern("/readme.eml"),  // 245: /readme.eml
    make_pattern("/handler"),  // 246: /handler
    make_pattern("/Recoveries/OSKey.txt"),  // 247: /Recoveries/OSKey.txt
    make_pattern("ftp.exe"),  // 248: ftp.exe
    make_pattern("\x03www\x05ghjgf\x04info\x00"),  // 249: |03|www|05|ghjgf|04|info|00|
    make_pattern("Mode=debug"),  // 250: Mode=debug
    make_pattern("forum_details.php"),  // 251: forum_details.php
    make_pattern("/random750x750.jpg?x="),  // 252: /random750x750.jpg?x=
    make_pattern("@@"),  // 253: @@
    make_pattern("/dms0"),  // 254: /dms0
    make_pattern("856"),  // 255: 856
    make_pattern("/httpodbc.dll"),  // 256: /httpodbc.dll
};

#endif // PATTERNS_H
