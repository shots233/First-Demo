// Fill in your copyright notice in the Description page of Project Settings.


#include "Notifies/ANS_ParryChainWindow.h"

#include "MyGameplayTags.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "Character/BaseCharacter.h"

void UANS_ParryChainWindow::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	// 与 ANS_SuperArmorWindow 相同的取用链路：动画所属角色 → ASC。
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

	// 死亡门控：已死亡的角色不再开放连锁窗口。
	if (ASC->HasMatchingGameplayTag(MyGameplayTags::Shared_Status_Dead))
	{
		return;
	}

	ASC->AddLooseGameplayTag(MyGameplayTags::DK_Status_ParryChainWindow);
	bTagAddedByThisWindow = true;
}

void UANS_ParryChainWindow::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
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

	// 离开连锁窗口。GA 跳回 Start 段时播放头离开本区间，引擎会补发本回调，
	// 标签随之摘除——一次窗口通过只允许连锁一次。
	ASC->RemoveLooseGameplayTag(MyGameplayTags::DK_Status_ParryChainWindow);
}
