// Fill out your copyright notice in the Description page of Project Settings.


#include "Notifies/ANS_SuperArmorWindow.h"

#include "MyGameplayTags.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "Character/BaseCharacter.h"

void UANS_SuperArmorWindow::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	// 与 ANS_HitReactWindow 相同的取用链路：动画所属角色 → ASC。
	ABaseCharacter* Character = MeshComp
		? Cast<ABaseCharacter>(MeshComp->GetOwner())
		: nullptr;
	UFirstAbilitySystemComponent* ASC = Character
		? Character->GetFirstAbilitySystemComponent()
		: nullptr;

	if (!ASC)
	{
		return;
	}

	// 死亡门控：已死亡的 BOSS 不再进入霸体（与 ANS_InvincibleWindow 的门控口径一致）。
	if (ASC->HasMatchingGameplayTag(MyGameplayTags::Shared_Status_Dead))
	{
		return;
	}

	// 进入霸体窗口：挂 Loose Tag。
	// GA_Boss_HitReact 被该标签阻挡；GA_Boss_ParryStagger 检测到该标签走"削韧不僵直"分支。
	ASC->AddLooseGameplayTag(MyGameplayTags::Boss_Status_Uninterruptible);
	bTagAddedByThisWindow = true;
}

void UANS_SuperArmorWindow::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	// 本窗口没有加过标签（死亡门控/ASC 缺失）时不做移除，防止计数打负。
	if (!bTagAddedByThisWindow)
	{
		return;
	}
	bTagAddedByThisWindow = false;

	ABaseCharacter* Character = MeshComp
		? Cast<ABaseCharacter>(MeshComp->GetOwner())
		: nullptr;
	UFirstAbilitySystemComponent* ASC = Character
		? Character->GetFirstAbilitySystemComponent()
		: nullptr;

	if (!ASC)
	{
		return;
	}

	// 离开霸体窗口。若蒙太奇被打断导致本回调漏发，
	// GA_Boss_FourCombo::EndAbility 还有 SetLooseGameplayTagCount(0) 兜底。
	ASC->RemoveLooseGameplayTag(MyGameplayTags::Boss_Status_Uninterruptible);
}
