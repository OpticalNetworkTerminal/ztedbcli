// Runtime ZTE DB CLI.
//
// Build for the target rootfs, then run on the device while cspd is alive:
//   ./ztedbcli TelnetCfg
//   ./ztedbcli dump -f table-list.txt -o runtime-db.xml
//   ./ztedbcli get TelnetCfg 0 Lan_Enable
//   ./ztedbcli set TelnetCfg 0 Lan_Enable 1
//   ./ztedbcli save
//
// This intentionally uses libdb.so shared-memory client APIs directly:
//   DBShmCliInit -> dbFindTbl -> dbGetDmValComm / dbSetValCommByRowNo
//
// It can validate offline dbunpack/dbpack work and can also replace a subset of
// "sendcmd 1 DB ..." when sendcmd is unavailable. It does not parse
// db_user_cfg.xml from flash.

#include <stddef.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>

#define DB_OFFSETOF(type, member) ((size_t)&((type *)0)->member)
#define DB_CONTAINER_OF(ptr, type, member) \
    ((type *)((char *)(ptr)-DB_OFFSETOF(type, member)))

struct linked_s;
typedef struct linked_s linked_t;

struct linked_s {
    linked_t *next;
};

typedef struct {
    const char *name;
    void *rsv[10];
    linked_t *next;
} tbl_col_t;

typedef struct {
    void *rsv[4];
    linked_t *next;
} tbl_row_t;

typedef struct {
    const char *name;
    void *rsv1[15];
    linked_t col_hdr;
    void *rsv2[10];
    int row_count;
    void *rsv3[3];
    linked_t row_hdr;
} tbl_t;

typedef struct {
    unsigned total;
    unsigned ok;
    unsigned failed;
} dump_stats_t;

int DBShmCliInit(void);
tbl_t *dbFindTbl(const char *tbl_name);
tbl_row_t *dbFindRowByNo(tbl_t *tb, int row_no);
tbl_row_t *dbAddRow(tbl_t *tb);
int dbDelRow(tbl_t *tb, tbl_row_t *row);
int dbGetDmValComm(tbl_t *tb, tbl_row_t *row, tbl_col_t *col, char *val,
                   unsigned size);
int dbGetValComm(const char *tbl_name, int row_no, const char *dm_name,
                 char *val, unsigned size);
int dbSetValCommByRowNo(tbl_t *tb, int row_no, const char *dm_name,
                        const char *value);
int dbAPISave(void);

static const char *default_tables[] = {
    "AccessNotifyCfg",
    "AppListCfg",
    "ArpAS",
    "AuthUserInfoProduct",
    "BPRInfo",
    "BULocationConf",
    "BUServiceConf",
    "BoardInfo",
    "CommSwitch",
    "DevAuthInfo",
    "DDNSClient",
    "DDNSService",
    "DHCP6SHostCfg",
    "DHCPSHostCfg",
    "DMSProduct",
    "DMZProduct",
    "DevInfo",
    "DevInfoProduct",
    "DhcpsCommonPdt",
    "DlTest",
    "EPONLlidConf",
    "EPONProduct",
    "EPONSilenceCount",
    "EthAlarmProduct",
    "EthGlobalConfProduct",
    "FTPServerCfgProduct",
    "FTPUser",
    "FTPUserProduct",
    "FWDMZ",
    "FWIPTV",
    "FalsifyDefend",
    "FwLevelProduct",
    "GPONCFG",
    "L2BAvailIF",
    "LAND",
    "LANInfo",
    "LanPrefixCfg",
    "LoopbackGlobalConf",
    "MAC",
    "MLDWan",
    "MQTTSrv",
    "MultiAPCfg",
    "MultiAPDomain",
    "MultiAPMaster",
    "MultiAPRoamCatonCfg",
    "NFCCONFProduct",
    "OMCICFG",
    "OPTICAL",
    "OnuSleepCfg",
    "P1905Cfg",
    "P1905ComCfg",
    "PWSaving",
    "PWSavingConf",
    "ParamInfoTbl",
    "PdtInterface",
    "Pingdiag",
    "PortControl",
    "PortControlProduct",
    "PortPriority",
    "PortalUrlProduct",
    "QOSApp",
    "QOSBasicCfgProduct",
    "QOSQueue",
    "QOSQueueCfgProduct",
    "RaCfg",
    "SAMBAUserProduct",
    "SNTPProduct",
    "SambaUser",
    "SecProtect",
    "SimulationDHCP",
    "SimulationPPPOE",
    "SleepCtrl",
    "TCONT",
    "TR069ClearPwd",
    "TR232BulkDataProduct",
    "TR232CSVProduct",
    "TR232HTTPProduct",
    "TR232JsonProduct",
    "TR232ParameterProduct",
    "TR232ProfileProduct",
    "TelnetCfg",
    "TimePolicy",
    "UNBINDCtlTbl",
    "VOIPCAP",
    "VOIPCIDCfg",
    "VOIPHomeLine",
    "VOIPPhyCallFeature",
    "VOIPPhyInterface",
    "VOIPVPCallFeature",
    "VOIPVPNUMBERPROC",
    "VOIPVPSERVICEKEY",
    "VOIPVPSPEEDDIAL",
    "VOIPVTCCfg",
    "VoIPBGWCfg",
    "VoIPBearInfo",
    "VoIPCSLine",
    "VoIPCapabilitiesCodec",
    "VoIPDMTimerCfg",
    "VoIPDSPCIDCfg",
    "VoIPDSPMISCCfg",
    "VoIPDTMFADVCfg",
    "VoIPExt",
    "VoIPFMediaCfg",
    "VoIPFaxModemRptCtrlCfg",
    "VoIPFaxT38Cfg",
    "VoIPFaxVBDCfg",
    "VoIPH248LineCfg",
    "VoIPH248MainCfg",
    "VoIPH248SubCfg",
    "VoIPHook",
    "VoIPHookVPCfg",
    "VoIPIADDiag",
    "VoIPIVRPsd",
    "VoIPLastSessionCfg",
    "VoIPLineCfg",
    "VoIPLineCodec",
    "VoIPLineHistoryCfg",
    "VoIPLineLastCfg",
    "VoIPMMediaCfg",
    "VoIPModemVBDCfg",
    "VoIPPhyNumCfg",
    "VoIPPoorQualityList",
    "VoIPPortCfg",
    "VoIPRINGERDescrptTable",
    "VoIPRINGEREventTable",
    "VoIPRINGERPatternTable",
    "VoIPRTCPADVCfg",
    "VoIPRTCPCfg",
    "VoIPRTPADVCfg",
    "VoIPRTPCfg",
    "VoIPRTPREDCfg",
    "VoIPRingerCfg",
    "VoIPRingerDescrptCfg",
    "VoIPRingerEventCfg",
    "VoIPRingerPatternCfg",
    "VoIPSIP",
    "VoIPSIPEventSubscribe",
    "VoIPSIPLan",
    "VoIPSIPLine",
    "VoIPSIPServer",
    "VoIPSIPTimer",
    "VoIPSLC112TESTCfg",
    "VoIPSLCCfgProduct",
    "VoIPSLCINFCfg",
    "VoIPSLCTIMECfg",
    "VoIPSRBwList",
    "VoIPSRDigitCollect",
    "VoIPSROfficeDigitMap",
    "VoIPSROfficeGroupPrefix",
    "VoIPSROfficePrefix",
    "VoIPSRPhyRefListEnable",
    "VoIPSRRouteDigitMap",
    "VoIPSRTPCfg",
    "VoIPSRTermination",
    "VoIPSessionCfg",
    "VoIPSimulateTest",
    "VoIPT38ADVCfg",
    "VoIPTONECfg",
    "VoIPTONEDescrptCfg",
    "VoIPTONEDescrptTable",
    "VoIPTONEEventCfg",
    "VoIPTONEEventTable",
    "VoIPTONEPatternCfg",
    "VoIPTONEPatternTable",
    "VoIPVMediaCfg",
    "VoIPVPCallTimer",
    "VoIPVPCodec",
    "VoIPVPDTMF",
    "VoIPVPLine",
    "VoIPVPNP",
    "VoIPVPService",
    "VoIPVoiceProcCfg",
    "VoIPVoiceProfile",
    "WLANBandSteering",
    "WLANBase",
    "WLANBaseProduct",
    "WLANCfg",
    "WLANCfgProduct",
    "WLANCountry",
    "WLANCountryProduct",
    "WLANPSK",
    "WLANPSKProduct",
    "WLANSetting",
    "WLANTimeCfgProduct",
    "WLANTimeProduct",
    "WLANWEP",
    "WLANWEPProduct",
    "WLANWMM",
    "WLANWMMProduct",
    "WLANWPS",
    "WLANWPSProduct",
    "WLCInfo",
    "WlanTime",
    "miniOLT",
    "sysRouteProduct",
    /* Tables observed in decrypted db_user_cfg.xml but absent from the
       original runtime dump table list. */
    "DBBase",
    "WAND",
    "WANCD",
    "WANC",
    "WANCServList",
    "WANCIP",
    "WANCIPOpts",
    "WANCIPOE",
    "WANCAuthExt",
    "WANCPPP",
    "WANCPPPComm",
    "BrGrp2ndIP",
    "DHCPSPool",
    "DHCPSOpts",
    "DHCPSBind",
    "DHCPSComm",
    "DHCPCComm",
    "STAMacList",
    "WlanTimeCfg",
    "WlanBandSteering",
    "IGMPProxy",
    "UserIF",
    "UserInfo",
    "AclCfg",
    "FWBase",
    "FWLevel",
    "FWALG",
    "FWIP",
    "FWURL",
    "FWSC",
    "FWPM",
    "FWPURL",
    "FWPMDEV",
    "FWParaFilter",
    "SNTP",
    "QOSBasic",
    "QOSClassification",
    "QOSPolicer",
    "QOSShaper",
    "L3Forwarding",
    "L3ForwardingRT",
    "MgtServer",
    "ParamAttr",
    "TR232BulkData",
    "TR232Profile",
    "TR232CSV",
    "TR232JSON",
    "TR232Parameter",
    "TR232HTTP",
    "TR232SBDC",
    "TR232REQPARM",
    "DNSSettings",
    "DNSHostsList",
    "UPnPCfg",
    "DDNSHostname",
    "WANDCommCfg",
    "Log",
    "FTPServerCfg",
    "RouteSYSRT",
    "CommSwit",
    "L2BBridge",
    "L2BFilter",
    "L2BMarking",
    "PortBinding",
    "Upgrade",
    "MacFilter",
    "RIPConf",
    "RIPIf",
    "UsbBakRst",
    "PRoute",
    "Tr069Queue",
    "AttrInfo",
    "VoIPVPNPPrefix",
    "VoIPHomeLine",
    "SrmCfg",
    "VOIPSLMTerm",
    "VOIPSLMWAN",
    "VOIPSLMGlobal",
    "VOIPSLMAD",
    "VOIPSLMSeviceKey",
    "VOIPSLMMedia",
    "VOIPSLMFaxMedia",
    "VOIPSLMVOIPCfg",
    "VOIPSrCommonConfigs",
    "VOIPSrTidConfigs",
    "VOIPSrGroupPrefix",
    "VOIPSrOfficeDiMap",
    "VOIPSrRouteDiMap",
    "VOIPSrBwListInf",
    "VOIPDRSLC",
    "VOIPDSPToneRing",
    "VOIPDSPT38Fax",
    "VOIPDSPVoiceGainEc",
    "VOIPDSPVadCng",
    "VOIPDSPDTMF",
    "VOIPDSPTone",
    "VOIPDSPJitterBuffer",
    "VOIPDSPFaxModemTone",
    "VOIPDSPFaxT38More",
    "VOIPDSPCID",
    "VOIPDSPFaxModemCtrl",
    "VOIPDSPFaxVbd",
    "VOIPDSPModemVbd",
    "VOIPDSPMisc",
    "VOIPRcaCommon",
    "VOIPSIPWANLine",
    "VOIPSIPLANLine",
    "VOIPSIPTimerCfg",
    "VOIPSIPServerCfg",
    "VOIPSIPCfg",
    "VOIPSIPExtraCfg",
    "VOIPSIPEventCfg",
    "VOIPSIPSupportedCfg",
    "VOIPCommTotal",
    "VOIPCommTTY",
    "VOIPCommPort",
    "VOIPExt",
    "VOIPIVRPassword",
    "VOIPHookCfg",
    "SambaCfg",
    "DMSCfg",
    "IGMPWan",
    "PrefixCfg",
    "PrefixIfDG",
    "PrefixBanPort",
    "DSSCfg",
    "DHCP6CComm",
    "MLDProxyCfg",
    "L3Forwarding6",
    "L3ForwardingRT6",
    "DevIPv6Ctrl",
    "IPV6PRoute",
    "TUNNEL46CFG",
    "PINGDiag",
    "LoopbackEthConf",
    "LoopbackVlanConf",
    "TimeSynInfo",
    "AutoEmulatConf",
    "DevAccInfo",
    "StgBlackList",
    "MacBlackList",
    "TrafficFastPathRecord",
    "FWPCTRL",
    "FWUser",
    "FWLIST",
    "FWTP",
    "FWAPP",
    "FWDuraTime",
    "VLANInfo",
    "WANIPBIND",
    "PortIsolation",
    "AUDITINFO",
    "MultiAPTopo",
    "MultiAPCacheMac",
    "P1905AcdBssCfg",
    "P1905AcdVenderCfg",
    "ArpBind",
    "SlaveIPTVBinding",
    "Wolink",
    "ParaSpl",
    "ParamInfo",
    "AndlinkNetlock",
    "WLANSlaveInfo",
    "CloudServer",
    "AppList",
    "P1905_ACD",
    "ALARMCONFIG",
    "ALARMPARM",
    "MONITORCONFIG",
    "MONITORPARM",
    "E8OPT60S",
    "E8OPT125S",
    "E8OPT16S",
    "E8OPT17S",
    "PONCfgProduct",
    "PDTWANCEXT",
    "SwitchCfg",
    "WancpppProduct",
    "VLANBIND",
    "MultiWancConfProduct",
    "MultiPortProduct",
    "IGMPIPTV",
    "CltLmt",
    "PDTCTUSERINFO",
    "E8PINGKEEP",
    "E8PINGKEEPCFG",
    "QosCvpA",
    "QOSRule",
    "QOSType",
    "MultiWancd",
    "ProInfoCfg",
    "E8Switcher",
    "E8STBBINDINFO",
    "DHCP6SPool",
    "DevBwBasic",
    "DevBandwidth",
    "E8forAutoTest",
    "Tr069InformParaExtend",
    "SELFBIND",
    "MTOUCH",
    "ARPDETECTConfig",
    "UserCtl",
    "E8RetranInterval",
    "MobileAppExInfo",
    "Tr069tcp",
    "WhiteListInform",
    "WlanPowerCheck",
    "VoIPSLCCfg",
    "EthRatelimitProduct",
    "MINIOLT",
    "NFCConfProduct",
    "FTTRInfo",
    "MonitorCollectorCfg",
    "MonitorCollectorParm",
    "AccessAlias",
    "AccessFamily",
    "UNBindUser",
};

static void xml_escape(FILE *out, const char *s) {
    const unsigned char *p = (const unsigned char *)s;

    if (s == NULL) {
        return;
    }

    while (*p) {
        switch (*p) {
        case '&':
            fputs("&amp;", out);
            break;
        case '<':
            fputs("&lt;", out);
            break;
        case '>':
            fputs("&gt;", out);
            break;
        case '"':
            fputs("&quot;", out);
            break;
        case '\'':
            fputs("&apos;", out);
            break;
        default:
            if (*p < 0x20 && *p != '\t' && *p != '\n' && *p != '\r') {
                fprintf(out, "&#x%02x;", *p);
            } else {
                fputc(*p, out);
            }
            break;
        }
        ++p;
    }
}

static int print_table_xml(FILE *out, const char *tbl_name) {
    tbl_t *tb = dbFindTbl(tbl_name);
    int row_num = 0;
    int row_limit = 0;
    int table_errors = 0;

    if (tb == NULL) {
        fprintf(stderr, "table(%s) not found\n", tbl_name);
        fprintf(out, "  <Tbl name=\"");
        xml_escape(out, tbl_name);
        fputs("\" missing=\"1\"/>\n", out);
        return 1;
    }

    fputs("  <Tbl name=\"", out);
    xml_escape(out, tb->name);
    fprintf(out, "\" RowCount=\"%d\">\n", tb->row_count);
    row_limit = tb->row_count > 0 ? tb->row_count : 0;

    for (row_num = 0; row_num < row_limit; ++row_num) {
        tbl_row_t *row = dbFindRowByNo(tb, row_num);
        linked_t *c = NULL;

        if (row == NULL) {
            fprintf(stderr, "row(%s[%d]) not found\n", tbl_name, row_num);
            fprintf(out, "    <Row No=\"%d\" missing=\"1\"/>\n", row_num);
            table_errors = 1;
            continue;
        }

        fprintf(out, "    <Row No=\"%d\">\n", row_num);
        for (c = tb->col_hdr.next; c != NULL && c != &tb->col_hdr;) {
            tbl_col_t *col = DB_CONTAINER_OF(c, tbl_col_t, next);
            linked_t *next_col = col->next;
            char val[16384];
            int rc = 0;

            memset(val, 0, sizeof(val));
            rc = dbGetDmValComm(tb, row, col, val, (unsigned)sizeof(val));

            fputs("      <DM name=\"", out);
            xml_escape(out, col->name);
            fputs("\" val=\"", out);
            xml_escape(out, val);
            if (rc != 0) {
                fprintf(out, "\" rc=\"%d", rc);
                table_errors = 1;
            }
            fputs("\"/>\n", out);

            c = next_col;
        }
        fputs("    </Row>\n", out);
    }

    fputs("  </Tbl>\n", out);
    return table_errors ? 1 : 0;
}

static void dump_stats_add(dump_stats_t *stats, int rc) {
    if (stats == NULL) {
        return;
    }
    ++stats->total;
    if (rc == 0) {
        ++stats->ok;
    } else {
        ++stats->failed;
    }
}

static int print_table_and_count(FILE *out, const char *tbl_name,
                                 dump_stats_t *stats) {
    int rc = print_table_xml(out, tbl_name);

    dump_stats_add(stats, rc);
    return rc;
}

static int print_table_file(FILE *out, const char *path, dump_stats_t *stats) {
    FILE *fp = fopen(path, "r");
    char line[256];
    int errors = 0;

    if (fp == NULL) {
        perror(path);
        return 1;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        char *p = line;
        char *end = NULL;

        while (*p == ' ' || *p == '\t') {
            ++p;
        }
        end = p + strlen(p);
        while (end > p &&
               (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ' ||
                end[-1] == '\t')) {
            --end;
        }
        *end = '\0';

        if (*p == '\0' || *p == '#') {
            continue;
        }
        errors += print_table_and_count(out, p, stats);
    }

    fclose(fp);
    return errors ? 1 : 0;
}

static int print_default_tables(FILE *out, dump_stats_t *stats) {
    size_t i = 0;
    int errors = 0;

    for (i = 0; i < sizeof(default_tables) / sizeof(default_tables[0]); ++i) {
        errors += print_table_and_count(out, default_tables[i], stats);
    }
    return errors ? 1 : 0;
}

static int cmd_get(const char *tbl_name, int row_no, const char *dm_name) {
    tbl_t *tb = dbFindTbl(tbl_name);
    tbl_row_t *row = NULL;
    linked_t *c = NULL;

    if (tb == NULL) {
        fprintf(stderr, "table(%s) not found\n", tbl_name);
        return 1;
    }
    if (row_no < 0 || row_no >= tb->row_count) {
        fprintf(stderr, "row out of range: table=%s row=%d RowCount=%d\n",
                tbl_name, row_no, tb->row_count);
        return 1;
    }

    row = dbFindRowByNo(tb, row_no);
    if (row == NULL) {
        fprintf(stderr, "row not found: table=%s row=%d RowCount=%d\n", tbl_name,
                row_no, tb->row_count);
        return 1;
    }

    for (c = tb->col_hdr.next; c != NULL && c != &tb->col_hdr;) {
        tbl_col_t *col = DB_CONTAINER_OF(c, tbl_col_t, next);
        linked_t *next_col = col->next;

        if (col->name != NULL && strcmp(col->name, dm_name) == 0) {
            char val[16384];
            int rc = 0;

            memset(val, 0, sizeof(val));
            rc = dbGetDmValComm(tb, row, col, val, (unsigned)sizeof(val));
            if (rc != 0) {
                fprintf(stderr, "get failed: table=%s row=%d dm=%s rc=%d\n",
                        tbl_name, row_no, dm_name, rc);
                return 1;
            }

            printf("%s\n", val);
            return 0;
        }

        c = next_col;
    }

    fprintf(stderr, "dm not found: table=%s row=%d dm=%s\n", tbl_name, row_no,
            dm_name);
    return 1;
}

static int cmd_set(const char *tbl_name, int row_no, const char *dm_name,
                   const char *value) {
    tbl_t *tb = dbFindTbl(tbl_name);
    int rc = 0;

    if (tb == NULL) {
        fprintf(stderr, "table(%s) not found\n", tbl_name);
        return 1;
    }

    rc = dbSetValCommByRowNo(tb, row_no, dm_name, value);
    if (rc != 0) {
        fprintf(stderr, "set failed: table=%s row=%d dm=%s value=%s rc=%d\n",
                tbl_name, row_no, dm_name, value, rc);
        return 1;
    }

    printf("OK set %s[%d].%s=%s\n", tbl_name, row_no, dm_name, value);
    return 0;
}

static int cmd_addrow(const char *tbl_name) {
    tbl_t *tb = dbFindTbl(tbl_name);
    tbl_row_t *row = NULL;

    if (tb == NULL) {
        fprintf(stderr, "table(%s) not found\n", tbl_name);
        return 1;
    }

    row = dbAddRow(tb);
    if (row == NULL) {
        fprintf(stderr, "addrow failed: table=%s\n", tbl_name);
        return 1;
    }

    printf("OK addrow %s RowCount=%d\n", tbl_name, tb->row_count);
    return 0;
}

static int cmd_delrow(const char *tbl_name, int row_no) {
    tbl_t *tb = dbFindTbl(tbl_name);
    tbl_row_t *row = NULL;
    int rc = 0;

    if (tb == NULL) {
        fprintf(stderr, "table(%s) not found\n", tbl_name);
        return 1;
    }

    row = dbFindRowByNo(tb, row_no);
    if (row == NULL) {
        fprintf(stderr, "row not found: table=%s row=%d\n", tbl_name, row_no);
        return 1;
    }

    rc = dbDelRow(tb, row);
    if (rc != 0) {
        fprintf(stderr, "delrow failed: table=%s row=%d rc=%d\n", tbl_name,
                row_no, rc);
        return 1;
    }

    printf("OK delrow %s[%d] RowCount=%d\n", tbl_name, row_no, tb->row_count);
    return 0;
}

static int cmd_save_direct(void) {
    int rc = dbAPISave();

    if (rc != 0) {
        fprintf(stderr, "save-direct failed: rc=%d\n", rc);
        return 1;
    }

    puts("OK save-direct");
    return 0;
}

static int cmd_save_direct_safe(void) {
    pid_t pid = fork();
    int status = 0;

    if (pid < 0) {
        fprintf(stderr, "fork failed: %s\n", strerror(errno));
        return 1;
    }

    if (pid == 0) {
        _exit(cmd_save_direct() == 0 ? 0 : 1);
    }

    if (waitpid(pid, &status, 0) < 0) {
        fprintf(stderr, "waitpid failed: %s\n", strerror(errno));
        return 1;
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }

    if (WIFSIGNALED(status)) {
        fprintf(stderr, "save-direct crashed: signal=%d\n", WTERMSIG(status));
        return 1;
    }

    fprintf(stderr, "save-direct ended unexpectedly: status=%d\n", status);
    return 1;
}

static int cmd_save(void) {
    int rc = system("sendcmd 1 DB save");

    if (rc == 0) {
        puts("OK save via sendcmd");
        return 0;
    }

    fprintf(stderr, "sendcmd save failed or unavailable: status=%d\n", rc);
    fprintf(stderr, "trying direct dbAPISave in child process\n");
    rc = cmd_save_direct_safe();
    if (rc != 0) {
        fprintf(stderr,
                "save failed: direct dbAPISave is unsafe on this firmware\n");
        return rc;
    }

    return 0;
}

static int parse_row_no(const char *s, int *out) {
    char *end = NULL;
    long value = strtol(s, &end, 10);

    if (s == NULL || *s == '\0' || end == NULL || *end != '\0') {
        return -1;
    }
    if (value < 0 || value > 2147483647L) {
        return -1;
    }
    *out = (int)value;
    return 0;
}

static int diag_dlopen_one(const char *name) {
    void *handle;
    const char *err;

    dlerror();
    handle = dlopen(name, RTLD_NOW | RTLD_GLOBAL);
    if (handle == NULL) {
        err = dlerror();
        fprintf(stderr, "dlopen %-18s FAIL: %s\n", name,
                err != NULL ? err : "unknown error");
        return 1;
    }

    printf("dlopen %-18s OK\n", name);
    return 0;
}

static void diag_maps_addr(uintptr_t addr) {
    FILE *fp;
    char line[256];
    unsigned long start;
    unsigned long end;

    fp = fopen("/proc/self/maps", "r");
    if (fp == NULL) {
        fprintf(stderr, "maps check skipped: %s\n", strerror(errno));
        return;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        if (sscanf(line, "%lx-%lx", &start, &end) == 2 &&
            (unsigned long)addr >= start && (unsigned long)addr < end) {
            printf("maps addr=0x%08lx occupied: %s", (unsigned long)addr,
                   line);
            fclose(fp);
            return;
        }
    }

    printf("maps addr=0x%08lx free before shmat\n", (unsigned long)addr);
    fclose(fp);
}

static int cmd_diag(void) {
    int errors = 0;
    int rc;
    int shmid;
    void *addr;
    tbl_t *tb;
    const uintptr_t db_addr = 0x60000000u;

    errors += diag_dlopen_one("libifscfapi.so");
    errors += diag_dlopen_one("libcfapi.so");
    errors += diag_dlopen_one("libdbcspview.so");

    diag_maps_addr(db_addr);

    errno = 0;
    shmid = shmget((key_t)0xffff, 0, 0);
    if (shmid == -1) {
        fprintf(stderr, "shmget key=0xffff FAIL: errno=%d (%s)\n", errno,
                strerror(errno));
    } else {
        printf("shmget key=0xffff OK: shmid=%d\n", shmid);

        errno = 0;
        addr = shmat(shmid, (void *)db_addr, 0);
        if (addr == (void *)-1) {
            fprintf(stderr,
                    "shmat shmid=%d addr=0x%08lx FAIL: errno=%d (%s)\n",
                    shmid, (unsigned long)db_addr, errno, strerror(errno));
        } else {
            printf("shmat shmid=%d addr=%p OK\n", shmid, addr);
            if (shmdt(addr) != 0) {
                fprintf(stderr, "shmdt addr=%p FAIL: errno=%d (%s)\n", addr,
                        errno, strerror(errno));
            }
        }
    }

    rc = DBShmCliInit();
    if (rc != 0) {
        fprintf(stderr, "DBShmCliInit WARN: %d\n", rc);
    } else {
        printf("DBShmCliInit OK\n");
    }

    tb = dbFindTbl("TelnetCfg");
    if (tb == NULL) {
        fprintf(stderr, "dbFindTbl TelnetCfg FAIL\n");
        return errors != 0 ? 1 : 2;
    }

    printf("dbFindTbl TelnetCfg OK: RowCount=%d\n", tb->row_count);
    return errors != 0 ? 1 : 0;
}

static void usage(const char *argv0) {
    fprintf(stderr,
            "Usage:\n"
            "  %s help\n"
            "  %s Table [...]\n"
            "  %s p|print [Table ...]\n"
            "  %s dump [-f table-list.txt] [-o output.xml]\n"
            "  %s get Table Row DM\n"
            "  %s set Table Row DM Value\n"
            "  %s addr|addrow Table\n"
            "  %s delr|delrow Table Row\n"
            "  %s save\n"
            "  %s save-direct\n"
            "  %s diag\n"
            "\n"
            "Examples:\n"
            "  %s p TelnetCfg\n"
            "  %s get TelnetCfg 0 Lan_Enable\n"
            "  %s get DevAuthInfo 0 Pass\n"
            "  %s set DevAuthInfo 0 Pass new_password\n"
            "  %s set TelnetCfg 0 Lan_Enable 1\n"
            "  %s save\n"
            "  %s dump -o runtime-db.xml\n"
            "  %s dump -f db-table-list.txt -o runtime-db.xml\n",
            argv0, argv0, argv0, argv0, argv0, argv0, argv0, argv0, argv0,
            argv0, argv0, argv0,
            argv0, argv0, argv0, argv0, argv0, argv0, argv0);
}

int main(int argc, char **argv) {
    const char *out_path = NULL;
    const char *list_path = NULL;
    FILE *out = stdout;
    dump_stats_t stats = {0, 0, 0};
    int i = 1;
    int errors = 0;
    int rc = 0;
    int row_no = 0;
    int dump_mode = 0;

    if (argc == 1) {
        usage(argv[0]);
        return 0;
    }

    if (argc > 1 &&
        (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0 ||
         strcmp(argv[1], "help") == 0)) {
        usage(argv[0]);
        return 0;
    }

    if (argc > 1 && strcmp(argv[1], "diag") == 0) {
        if (argc != 2) {
            usage(argv[0]);
            return 2;
        }
        return cmd_diag();
    }

    if (argc > 1 && strcmp(argv[1], "save") == 0) {
        if (argc != 2) {
            usage(argv[0]);
            return 2;
        }
        return cmd_save();
    }

    if (argc > 1 && strcmp(argv[1], "save-direct") == 0) {
        if (argc != 2) {
            usage(argv[0]);
            return 2;
        }
        return cmd_save_direct_safe();
    }

    rc = DBShmCliInit();
    if (rc != 0) {
        fprintf(stderr, "DBShmCliInit warning: %d; continuing\n", rc);
    }

    if (argc > 1 && strcmp(argv[1], "get") == 0) {
        if (argc != 5 || parse_row_no(argv[3], &row_no) != 0) {
            usage(argv[0]);
            return 2;
        }
        return cmd_get(argv[2], row_no, argv[4]);
    }

    if (argc > 1 && strcmp(argv[1], "set") == 0) {
        if (argc != 6 || parse_row_no(argv[3], &row_no) != 0) {
            usage(argv[0]);
            return 2;
        }
        return cmd_set(argv[2], row_no, argv[4], argv[5]);
    }

    if (argc > 1 &&
        (strcmp(argv[1], "addr") == 0 || strcmp(argv[1], "addrow") == 0)) {
        if (argc != 3) {
            usage(argv[0]);
            return 2;
        }
        return cmd_addrow(argv[2]);
    }

    if (argc > 1 &&
        (strcmp(argv[1], "delr") == 0 || strcmp(argv[1], "delrow") == 0)) {
        if (argc != 4 || parse_row_no(argv[3], &row_no) != 0) {
            usage(argv[0]);
            return 2;
        }
        return cmd_delrow(argv[2], row_no);
    }

    if (argc > 1 && (strcmp(argv[1], "p") == 0 || strcmp(argv[1], "print") == 0)) {
        i = 2;
    } else if (argc > 1 && strcmp(argv[1], "dump") == 0) {
        dump_mode = 1;
        i = 2;
    }

    while (i < argc) {
        if (strcmp(argv[i], "-o") == 0) {
            if (++i >= argc) {
                usage(argv[0]);
                return 2;
            }
            out_path = argv[i++];
        } else if (strcmp(argv[i], "-f") == 0) {
            if (++i >= argc) {
                usage(argv[0]);
                return 2;
            }
            list_path = argv[i++];
        } else if (strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            break;
        }
    }

    if (out_path != NULL) {
        out = fopen(out_path, "w");
        if (out == NULL) {
            perror(out_path);
            return 1;
        }
    }

    fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", out);
    fputs("<RuntimeDB>\n", out);

    if (list_path != NULL) {
        errors += print_table_file(out, list_path, &stats);
    } else if (dump_mode) {
        errors += print_default_tables(out, &stats);
    }

    for (; i < argc; ++i) {
        errors += print_table_and_count(out, argv[i], &stats);
    }

    fputs("</RuntimeDB>\n", out);

    fprintf(stderr, "dump summary: total=%u ok=%u failed=%u\n", stats.total,
            stats.ok, stats.failed);

    if (out != stdout) {
        fclose(out);
    }
    return errors ? 1 : 0;
}
