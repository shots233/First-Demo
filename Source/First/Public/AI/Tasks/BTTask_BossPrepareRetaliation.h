#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_BossPrepareRetaliation.generated.h"

// 放在原攻击链的 Select Attack 前；没有反击请求时直接通过。
UCLASS(meta=(DisplayName="Boss Prepare Retaliation"))
class FIRST_API UBTTask_BossPrepareRetaliation : public UBTTaskNode
{
	GENERATED_BODY()
public:
	UBTTask_BossPrepareRetaliation();
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
};
