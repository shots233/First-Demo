#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Decorators/BTDecorator_Cooldown.h"
#include "GameplayTagContainer.h"
#include "TimerManager.h"
#include "BTDecorator_BossAttackCooldown.generated.h"

class UAbilitySystemComponent;
class UGameplayAbility;

// 普通冷却到期只放行自然选路；仅有效反击请求主动抢占。招式冷却仍由 GAS/选招任务负责。
UCLASS(meta=(DisplayName="Boss Attack Cooldown"))
class FIRST_API UBTDecorator_BossAttackCooldown : public UBTDecorator_Cooldown
{
	GENERATED_BODY()
public:
	UBTDecorator_BossAttackCooldown(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Wait after the whole attack ends. Off preserves the original branch-exit Cool Down Time. */
	UPROPERTY(EditAnywhere, Category="Attack Recovery", meta=(DisplayName="Use Post Attack Recovery"))
	bool bUsePostAttackRecovery = false;

	/** Sampled once per completed or interrupted attack. Equal bounds give a fixed recovery time. */
	UPROPERTY(EditAnywhere, Category="Attack Recovery", meta=(EditCondition="bUsePostAttackRecovery", ClampMin="0.0", Units="s"))
	float RecoveryTimeMin = 3.f;

	UPROPERTY(EditAnywhere, Category="Attack Recovery", meta=(EditCondition="bUsePostAttackRecovery", ClampMin="0.0", Units="s"))
	float RecoveryTimeMax = 6.f;

	virtual bool CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;
	virtual FString GetStaticDescription() const override;
	virtual void DescribeRuntimeValues(const UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory,
		EBTDescriptionVerbosity::Type Verbosity, TArray<FString>& Values) const override;
	virtual void InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryInit::Type InitType) const override;
	virtual void CleanupMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryClear::Type CleanupType) const override;
	virtual void OnInstanceCreated(UBehaviorTreeComponent& OwnerComp) override;
	virtual void OnInstanceDestroyed(UBehaviorTreeComponent& OwnerComp) override;

	// Runtime diagnostics on this BOSS's node instance; values never reroll during a query.
	float GetSampledRecoveryDuration() const { return Recovery.SampledDuration; }
	float GetRecoveryTimeRemaining() const;
protected:
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual void OnNodeDeactivation(FBehaviorTreeSearchData& SearchData, EBTNodeResult::Type NodeResult) override;
private:
	void HandleAbilityActivated(UGameplayAbility* Ability);
	void HandleAttackTagChanged(FGameplayTag Tag, int32 Count);
	void ConfirmAttackEnded();
	void StopObserving();
	void GetRecoveryRange(float& OutMin, float& OutMax) const;
	static bool IsRecoveryAttack(const UGameplayAbility* Ability);

	struct FRecoveryState
	{
		double ReadyTime = 0.0;
		double PendingEndTime = -1.0;
		float SampledDuration = 0.f;
		bool bTrackingAttack = false;
	};
	// Nodes are instanced. Only full initialization/destruction resets this state;
	// StoreSubtree/RestoreSubtree keep an already sampled deadline.
	mutable FRecoveryState Recovery;
	TWeakObjectPtr<UAbilitySystemComponent> ObservedAbilitySystem;
	TWeakObjectPtr<UWorld> ObservedWorld;
	FDelegateHandle AbilityActivatedHandle;
	FDelegateHandle AttackTagHandle;
	FTimerHandle ConfirmEndTimer;
	float TimeUntilCheck = 0.f;
};
