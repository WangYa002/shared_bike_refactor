#include "user_event_handler.h"
#include "DispatchMsgService.h"
#include "sqlConnection.h"
#include "iniconfig.h"
#include "user_service.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "thread.h"


UserEventHandler::UserEventHandler() : iEventHandler("UserEventHandler") {
	//实现订阅事件的处理
	DispatchMsgService::getInstance()->subscribe(EVENTID_GET_MOBILE_CODE_REQ, this);
	DispatchMsgService::getInstance()->subscribe(EVENTID_LOGIN_REQ, this);
	thread_mutex_create(&pm_);	//创建互斥锁
	
}

UserEventHandler::~UserEventHandler() {
	//实现订阅事件的退订
	DispatchMsgService::getInstance()->unsubscribe(EVENTID_GET_MOBILE_CODE_REQ, this);
	DispatchMsgService::getInstance()->unsubscribe(EVENTID_LOGIN_REQ, this);
	thread_mutex_destroy(&pm_); //销毁互斥锁
}

iEevent* UserEventHandler::handle(const iEevent* ev) {
	if (ev == NULL) {
		//LOG_ERROR("input ev is NULL")
		printf("input ev is NULL");
	}
	u32 eid = ev->get_eid();
	//请求事件
	if (eid == EVENTID_GET_MOBILE_CODE_REQ)
	{
		return handle_mobile_code_req((MobileCodeReqEv*)ev);
	}

	else if (eid == EVENTID_LOGIN_REQ) {
		return handle_login_req((LoginReqEv*) ev);
	}
	else if (eid == EVENTID_RECHARGE_REQ) {
		// return handle_recharge_req((RechargeEv*) ev);
	}
	else if (eid == EVENTID_GET_ACCOUNT_BALANCE_REQ) {
		//return handle_get_account_balance_req((GetAccountBalanceEv*) ev);
	}
	else if (eid == EVENTID_LIST_ACCOUNT_RECORDS_REQ) {
		//return handle_list_account_records_req((ListAccountRecordsReqEv*) ev);
	}
	return NULL;
}

MobileCodeRspEv* UserEventHandler::handle_mobile_code_req(MobileCodeReqEv* ev)
{
	i32 icode = 0;
	std::string mobile_ = ev->get_mobile();   //手机号

	LOG_DEBUG("try to get moblie phone %s validate code .", mobile_.c_str());
	//printf("try to get moblie phone %s validate code .\n", mobile_.c_str());

	icode = code_gen();

	thread_mutex_lock(&pm_);
	m2c_[mobile_] = icode;         //手机号 =》验证码
	thread_mutex_unlock(&pm_);

	printf("mobile: %s, code: %d\n", mobile_.c_str(), icode);

	return new MobileCodeRspEv(ERRC_SUCCESS, icode);
}

LoginRspEv* UserEventHandler::handle_login_req(LoginReqEv* ev) {
	LoginRspEv* loginEv = nullptr;

	i32 icode = ev->get_icode(); //验证码
	std::string mobile_ = ev->get_mobile(); //手机号

	LOG_DEBUG("try to handle login ev, mobile = %s, code = %d.", mobile_.c_str(), icode);

	thread_mutex_lock(&pm_);
	auto iter = m2c_.find(mobile_);

	if (((iter != m2c_.end()) && (icode != iter->second)) || (iter == m2c_.end())) {
		loginEv = new LoginRspEv(ERRC_INVALID_DATA);
	}

	thread_mutex_unlock(&pm_);

	if (loginEv) return loginEv;

	//如果验证成功,则需要判断用户数据是否存在，不存在则插入用户记录
	std::shared_ptr<MysqlConnection> mysqlconn(new MysqlConnection);

	st_env_config conf_args = Iniconfig::getInstance()->getconfig();
	if (!mysqlconn->init(conf_args.db_ip.c_str(), conf_args.db_port, conf_args.db_user.c_str(), conf_args.db_pwd.c_str(), conf_args.db_name.c_str()))
	{
		LOG_ERROR("UserEventHandler::handle_login_req - Database init faile.EXIT!\n");
		return new LoginRspEv(ERRO_PROCCESS_FAILED);
	}

	UserService us(mysqlconn);
	bool result = false;

	if (!us.exist(mobile_)) {
		result = us.insert(mobile_);

		if (!result) {
			LOG_ERROR("insert user mobile(%s) to db failed.", mobile_.c_str());
			return new LoginRspEv(ERRO_PROCCESS_FAILED);
		}

	}

	return new LoginRspEv(ERRC_SUCCESS);
}



i32 UserEventHandler::code_gen() {
	i32 code = 0;
	srand((unsigned int)time(NULL));

	code = (unsigned int)(rand() % (999999 - 100000) + 100000);
	return code;

}


