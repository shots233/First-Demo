#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "FirstGuardHitEventData.generated.h"

// 每次普通格挡事件独立携带参数，避免连续命中共用可变状态。
UCLASS(BlueprintType, Transient)
class FIRST_API UFirstGuardHitEventData : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category="Defense", meta=(Units="cm"))
	float GuardPushbackDistance = 0.f;

	UPROPERTY(BlueprintReadOnly, Category="Defense", meta=(Units="s"))
	float GuardPushbackDuration = 0.f;
};
