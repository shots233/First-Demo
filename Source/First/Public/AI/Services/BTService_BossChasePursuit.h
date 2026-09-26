#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BTService_BossChasePursuit.generated.h"

// 仅保留已保存资产的类引用；不再启动技能，请改用任务分支。
UCLASS(meta=(DisplayName="Boss Chase Pursuit (Legacy - Remove)"))
class FIRST_API UBTService_BossChasePursuit : public UBTService
{
	GENERATED_BODY()

public:
	UBTService_BossChasePursuit();

};
