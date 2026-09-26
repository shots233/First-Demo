#include "AI/Tasks/BTTask_BossPrepareRetaliation.h"

#include "AIController.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Character/BossCharacter.h"
#include "Components/Combat/FirstBossRetaliationComponent.h"
#include "MyGameplayTags.h"

UBTTask_BossPrepareRetaliation::UBTTask_BossPrepareRetaliation()
{
	NodeName = TEXT("Boss Prepare Retaliation");
}

EBTNodeResult::Type UBTTask_BossPrepareRetaliation::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	const AAIController* Controller = OwnerComp.GetAIOwner();
	ABossCharacter* Boss = Controller ? Cast<ABossCharacter>(Controller->GetPawn()) : nullptr;
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!IsValid(Boss) || !Boss->HasAuthority() || !BB || !Boss->RetaliationComponent) return EBTNodeResult::Failed;
	AActor* Target = Cast<AActor>(BB->GetValueAsObject(TEXT("TargetActor")));
	if (!IsValid(Target)) return EBTNodeResult::Failed;
	UFirstBossRetaliationComponent* Retaliation = Boss->RetaliationComponent;
	if (Retaliation->bRetaliationPending)
	{
		return Retaliation->PrepareRetaliation(Target) ? EBTNodeResult::Succeeded : EBTNodeResult::Failed;
	}
	// 普通攻击继续原流程。请求在装饰器检查之后过期时，不得借此解除硬直。
	const UFirstAbilitySystemComponent* ASC = Boss->GetFirstAbilitySystemComponent();
	return ASC && !ASC->HasMatchingGameplayTag(MyGameplayTags::Boss_Status_Staggered)
		? EBTNodeResult::Succeeded : EBTNodeResult::Failed;
}
