#include "AI/Services/BTService_BossChasePursuit.h"

UBTService_BossChasePursuit::UBTService_BossChasePursuit()
{
	NodeName = TEXT("Boss Chase Pursuit (Legacy - Remove)");
	bNotifyTick = false;
	bNotifyBecomeRelevant = false;
	bNotifyCeaseRelevant = false;
}
