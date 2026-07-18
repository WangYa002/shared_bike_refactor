#include "ievent.h"

iEevent::iEevent(u32 eid, u32 sn) : eid_(eid), sn_(sn)
{
}

iEevent::~iEevent()
{

}

u32 iEevent::generateSeqNo()
{
	static u32 sn = 0;
	return sn++;
}
