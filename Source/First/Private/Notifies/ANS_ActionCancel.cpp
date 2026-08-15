// Fill out your copyright notice in the Description page of Project Settings.


#include "Notifies/ANS_ActionCancel.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "MyGameplayTags.h"
#include "Abilities/GameplayAbilityTypes.h"

namespace 
{
	// 该函数只负责把动画时间点转换成 Gameplay Event。
	// 它不直接播放 Dodge，也不直接添加 ASC 标签。
	void SendActionCancelWindowEvent(USkeletalMeshComponent* MeshComp, const FGameplayTag& EventTag, const FGameplayTagContainer& GrantedCancelTags)
	{
		AActor* OwnerActor = MeshComp? MeshComp->GetOwner() : nullptr;
		if (!OwnerActor || GrantedCancelTags.IsEmpty())
		{
			return;
		}
		
		FGameplayEventData EventData;
		EventData.Instigator = OwnerActor;
		EventData.Target = OwnerActor;
		
		// FGameplayEventData 没有专门名为 CancelTags 的字段。
		// 此项目约定：InstigatorTags 用来携带“本窗口授予哪些取消权限”。
		// 它不会自动写入 ASC，真正添加标签的是 Attack Ability。
		
		EventData.InstigatorTags = GrantedCancelTags;
		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(OwnerActor, EventTag, EventData);
		
	}
}



void UANS_ActionCancel::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	SendActionCancelWindowEvent(MeshComp,MyGameplayTags::DK_Event_ActionCancelWindow_Open,GrantedCancelTags);
}

void UANS_ActionCancel::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);
	
	SendActionCancelWindowEvent(MeshComp,MyGameplayTags::DK_Event_ActionCancelWindow_Close,GrantedCancelTags);
}
