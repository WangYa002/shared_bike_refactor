#ifndef NS_IEVENT_HANDLER_H_
#define NS_IEVENT_HANDLER_H_

#include "ievent.h"
#include "eventtype.h"

class iEventHandler
{
public:
	virtual iEevent* handle(const iEevent* ev) { return NULL; };
	virtual ~iEventHandler() {};
	iEventHandler(const char *name):name_(name) {};
	std::string& get_name() { return name_; };
private:
	std::string name_;
};


#endif // !NS_IEVENT_HANDLER_H_

