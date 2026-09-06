// Fill out your copyright notice in the Description page of Project Settings.


#include "Notifies/ANS_HitReactWindow.h"

#include "MyGameplayTags.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "Character/BaseCharacter.h"

void UANS_HitReactWindow::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
                                      float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	// 通知状态（Notify State）是动画资产里的一个"时间段"：
	// 动画播放到这段的开始，引擎自动调用 NotifyBegin；离开这段，自动调用 NotifyEnd。
	// 这里先拿到动画所属的角色，再拿到它的 ASC（GAS 组件）。
	ABaseCharacter* Character = MeshComp
		? Cast<ABaseCharacter>(MeshComp->GetOwner())
		: nullptr;
	UFirstAbilitySystemComponent* ASC = Character
		? Character->GetFirstAbilitySystemComponent()
		: nullptr;

	// 拿不到 ASC 说明角色没有 GAS（正常不会发生，但防御性判空更安全）。
	if (!ASC)
	{
		return;
	}

	// 进入受击窗口：给 ASC 加一个 Loose Tag。
	// 之后玩家命中 BOSS 时，GA_Boss_HitReact 检查到这个标签才允许打断。
	ASC->AddLooseGameplayTag(MyGameplayTags::Boss_Status_HitReactWindow);
}

void UANS_HitReactWindow::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	// 离开受击窗口：同样先取角色和 ASC。
	ABaseCharacter* Character = MeshComp
		? Cast<ABaseCharacter>(MeshComp->GetOwner())
		: nullptr;
	UFirstAbilitySystemComponent* ASC = Character
		? Character->GetFirstAbilitySystemComponent()
		: nullptr;

	// 防御性判空。
	if (!ASC)
	{
		return;
	}

	// 移除标签：窗口结束，之后命中不再打断（只扣血）。
	ASC->RemoveLooseGameplayTag(MyGameplayTags::Boss_Status_HitReactWindow);
}
