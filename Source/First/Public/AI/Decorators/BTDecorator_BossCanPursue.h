#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTDecorator.h"
#include "GameplayTagContainer.h"
#include "BTDecorator_BossCanPursue.generated.h"

// 追击策略属于行为树；已发动的技能不会因距离变化自停。
UCLASS(meta=(DisplayName="Boss Can Pursue"))
class FIRST_API UBTDecorator_BossCanPursue : public UBTDecorator
{
	GENERATED_BODY()
public:
	UBTDecorator_BossCanPursue();
	virtual void InitializeFromAsset(UBehaviorTree& Asset) override;
	virtual FString GetStaticDescription() const override;
	UPROPERTY(EditAnywhere, Category="Pursuit")
	FGameplayTag AbilityTag;
	UPROPERTY(EditAnywhere, Category="Pursuit", meta=(ClampMin="1", Units="cm"))
	float MinTriggerRange = 500.f;
	UPROPERTY(EditAnywhere, Category="Pursuit", meta=(ClampMin="1", Units="cm"))
	float MaxTriggerRange = 650.f;
	UPROPERTY(EditAnywhere, Category="Blackboard")
	FBlackboardKeySelector TargetActorKey;
	UPROPERTY(EditAnywhere, Category="Blackboard")
	FBlackboardKeySelector ShouldChaseKey;
	UPROPERTY(EditAnywhere, Category="Blackboard")
	FBlackboardKeySelector PlayerTooFarKey;
	UPROPERTY(EditAnywhere, Category="Blackboard")
	FBlackboardKeySelector BusyKey;
	UPROPERTY(EditAnywhere, Category="Blackboard")
	FBlackboardKeySelector ProvokedKey;
protected:
	virtual bool CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;
	virtual void OnBecomeRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
private:
	float TimeUntilCheck = 0.f;
};
