#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "GameplayAbilitySpecHandle.h"
#include "GameplayTagContainer.h"
#include "BTTask_ActivateAbilityAndWait.generated.h"

class AActor;
class UAbilitySystemComponent;
struct FAbilityEndedData;

/** Starts one granted ability with a target and owns it until that ability ends. */
UCLASS(meta = (DisplayName = "Activate Ability And Wait"))
class FIRST_API UBTTask_ActivateAbilityAndWait : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_ActivateAbilityAndWait();

	/** Must identify exactly one granted ability by an exact asset-tag match. */
	UPROPERTY(EditAnywhere, Category = "Ability")
	FGameplayTag AbilityTag;

	/** The selected actor is sent in GameplayEventData.Target and retained for this execution. */
	UPROPERTY(EditAnywhere, Category = "Ability")
	FBlackboardKeySelector TargetActorKey;

	virtual void InitializeFromAsset(UBehaviorTree& Asset) override;
	virtual FString GetStaticDescription() const override;
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void OnInstanceDestroyed(UBehaviorTreeComponent& OwnerComp) override;

protected:
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual void OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory,
		EBTNodeResult::Type TaskResult) override;

private:
	void HandleAbilityEnded(const FAbilityEndedData& EndedData);
	void ClearExecution(bool bCancelOwnedAbility);
	void CompleteExecution(UBehaviorTreeComponent& OwnerComp, EBTNodeResult::Type Result,
		bool bCancelOwnedAbility);

	TWeakObjectPtr<UAbilitySystemComponent> ActiveAbilitySystem;
	TWeakObjectPtr<UBehaviorTreeComponent> ActiveTree;
	TWeakObjectPtr<AActor> ActiveTarget;
	FGameplayAbilitySpecHandle ActiveAbilityHandle;
	FDelegateHandle AbilityEndedDelegateHandle;
	uint32 ExecutionGeneration = 0;
	bool bInsideExecuteTask = false;
	bool bOwnsAbility = false;
	bool bReceivedAbilityEnd = false;
	bool bAbilityWasCancelled = false;
};
