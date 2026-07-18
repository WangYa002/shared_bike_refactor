#include "events_def.h"

std::ostream& MobileCodeReqEv::dump(std::ostream& out) const {
	out << "MobileCodeReq sn=" << get_sn() << ",";
	out << "mobile=" << msg_.mobile();
	out << std::endl;

	return out;
}


std::ostream& MobileCodeRspEv::dump(std::ostream& out) const {
	out << "MobileCodeRsp sn=" << get_sn() << ",";
	out << "MobileCodeRsp code=" << get_code() << ",";
	out << "MobileCodeRsp icode=" << get_icode()  << ",";
	out << "MobileCodeRsp description=" << msg_.data() << ",";
	out << std::endl;

	return out;
}


std::ostream& LoginReqEv::dump(std::ostream& out) const
{
	out << "LoginReqEv sn = " << get_sn() << ","
		<< "mobile = " << msg_.mobile() << ","
		<< "icode: " << msg_.icode() << std::endl;
}

std::ostream& LoginRspEv::dump(std::ostream& out) const
{
	out << "LoginRspEv sn = " << get_sn() << ","
		<< "code = " << msg_.code() << ","
		<< "describe: " << msg_.desc() << std::endl;

	return out;
}

