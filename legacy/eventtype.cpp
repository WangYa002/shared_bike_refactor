#include "eventtype.h"

static EErrorReason EER[] = {
	{ERRC_SUCCESS, "ok."},
	{ERRC_INVALID_MSG, "Invalid message."},
	{ERRC_INVALID_DATA, "Invalid data."},
	{ERRC_METHOD_NOT_ALLOWED, "Method not allowed."},
	{ERRO_PROCCESS_FAILED, "Proccess failed."},
	{ERRO_BIKE_IS_TOOK, "Bike is took."},
	{ERRO_BIKE_IS_RUNNING , "Bike is running."},
	{ERRO_BIKE_IS_DAMAGED, "Bike is damaged."},
	{ERR_NULL, "Undefined."}
};


static EidString ES[] = {
	{EVENTID_GET_MOBILE_CODE_REQ, "EVENTID_GET_MOBILE_CODE_REQ"},
	{EVENTID_GET_MOBILE_CODE_RSP, "EVENTID_GET_MOBILE_CODE_RSP"},
	{EVENTID_LOGIN_REQ, "EVENTID_LOGIN_REQ"},
	{EVENTID_LOGIN_RSP, "EVENTID_LOGIN_RSP"},
	{EVENTID_RECHARGE_REQ, "EVENTID_RECHARGE_REQ"},
	{EVENTID_RECHARGE_RSP, "EVENTID_RECHARGE_RSP"},
	{EVENTID_GET_ACCOUNT_BALANCE_REQ, "EVENTID_GET_ACCOUNT_BALANCE_REQ"},
	{EVENTID_ACCOUNT_BALANCE_RSP, "EVENTID_ACCOUNT_BALANCE_RSP"},
	{EVENTID_LIST_ACCOUNT_RECORDS_REQ, "EVENTID_LIST_ACCOUNT_RECORDS_REQ"},
	{EVENTID_ACCOUNT_RECORDS_RSP, "EVENTID_ACCOUNT_RECORDS_RSP"},
	{EVENTID_LIST_TRAVELS_REQ, "EVENTID_LIST_TRAVELS_REQ"},
	{EVENTID_LIST_TRAVELS_RSP, "EVENTID_LIST_TRAVELS_RSP"},
	{EVENTID_EXIT_RSP, "EVENTID_EXIT_RSP"},
	{EVENTID_UNKNOWN, "EVENTID_UNKNOWN"}
};



const char* getReasonByErrorCode(i32 code)
{
	i32 i = 0;
	for (i = 0;EER[i].code != ERR_NULL; i++) {
		if (EER[i].code == code) {
			return EER[i].reason;
		}
	}

	return EER[i].reason; //"Undefined"
}


const char* getEidToString(i32 eid)
{
	i32 i = 0;
	for (i = 0; ES[i].eid != EVENTID_UNKNOWN; ++i) {
		if (ES[i].eid == eid) {
			return ES[i].phrase;
		}
	}

	return ES[i].phrase; //"EVENTID_UNKOWN"
}