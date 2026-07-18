#ifndef BRKS_COMMON_EVENTS_DEF_H_
#define BRKS_COMMON_EVENTS_DEF_H_

#include "ievent.h"
#include "bike.pb.h"

class MobileCodeReqEv : public iEevent
{

public:
	MobileCodeReqEv(const std::string& mobile) :iEevent(EVENTID_GET_MOBILE_CODE_REQ, iEevent::generateSeqNo())
	{
		msg_.set_mobile(mobile);
	}

	const std::string& get_mobile() { return msg_.mobile(); }
	virtual std::ostream& dump(std::ostream& out) const;
	virtual i32 ByteSize() { return msg_.ByteSizeLong(); }
	virtual bool SerializeToArray(char* buf, int len) { return msg_.SerializeToArray(buf, len); };

private:
	tutorial::mobile_request msg_;

};


class MobileCodeRspEv : public iEevent
{
public:
	MobileCodeRspEv(i32 icode, i32 code) : iEevent(EVENTID_GET_MOBILE_CODE_RSP, iEevent::generateSeqNo())
	{
		msg_.set_code(code);
		msg_.set_icode(icode);
		msg_.set_data(getReasonByErrorCode(icode));
	}
	
	 i32 get_code() const { return msg_.code(); };
	 i32 get_icode() const{ return msg_.icode(); };
	 const std::string& get_data() { return msg_.data(); };

	virtual std::ostream& dump(std::ostream& out) const;
	virtual i32 ByteSize() { return msg_.ByteSize(); };
	virtual bool SerializeToArray(char* buf, int len) { return msg_.SerializeToArray(buf, len); };

private:
	tutorial::mobile_response msg_;

};


class ExitRspEv : public iEevent
{
public:
	ExitRspEv() : iEevent(EVENTID_EXIT_RSP, iEevent::generateSeqNo()){}
	//~ExitRspEv();
private:

};

class LoginReqEv : public iEevent //登录验证请求
{
public:
	LoginReqEv(const std::string& mobile, i32 icode) : iEevent(EVENTID_LOGIN_REQ, iEevent::generateSeqNo()) {
		msg_.set_mobile(mobile);
		msg_.set_icode(icode);
	}
	
	const std::string& get_mobile() { return msg_.mobile(); }
	const i32 get_icode() { return msg_.icode(); }

	virtual std::ostream& dump(std::ostream& out) const;
	virtual i32 ByteSize() { return msg_.ByteSizeLong(); }
	virtual bool SerializeToArray(char* buf, int len) { return msg_.SerializeToArray(buf, len); }


private:
	tutorial::login_request msg_;

};


class LoginRspEv : public iEevent //登录验证响应

{
public:
	LoginRspEv(i32 code) : iEevent(EVENTID_LOGIN_RSP, iEevent::generateSeqNo()) //登录验证响应
	{
		msg_.set_code(code);
		msg_.set_desc(getReasonByErrorCode(code));
	}
	const i32 get_code() { return msg_.code(); }
	const std::string& get_desc() { return msg_.desc(); }

	virtual std::ostream& dump(std::ostream& out) const;
	virtual i32 ByteSize() { return msg_.ByteSizeLong(); }
	virtual bool SerializeToArray(char* buf, int len) { return msg_.SerializeToArray(buf, len); }


private:
	tutorial::login_response msg_;

};







#endif // !BRKS_COMMON_EVENTS_DEF_H_

