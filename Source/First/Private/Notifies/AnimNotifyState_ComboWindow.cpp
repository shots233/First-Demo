// Fill out your copyright notice in the Description page of Project Settings.


#include "Notifies/AnimNotifyState_ComboWindow.h"
#include "MyGameplayTags.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Animation/AnimSequenceBase.h"

namespace
{
	// 作用：把动画时间点转换为发送给 Mesh Owner 的 GameplayEvent，供正在运行的 Ability 监听。
	void SendComboWindowEvent(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		UAnimNotifyState_ComboWindow* NotifyState, const FGameplayTag& EventTag)
	{
		AActor* OwnerActor = MeshComp ? MeshComp->GetOwner() : nullptr;
		if (!OwnerActor)
		{
			return;
		}

		// 来源随事件传递；Notify 资产在角色之间共享，不能保存运行时窗口状态。
		FGameplayEventData EventData;
		EventData.EventTag = EventTag;
		EventData.Instigator = OwnerActor;
		EventData.Target = OwnerActor;
		EventData.OptionalObject = NotifyState;
		EventData.OptionalObject2 = Animation;

		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(OwnerActor,EventTag,EventData);
	}
}

void UAnimNotifyState_ComboWindow::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	UE_LOG(LogTemp, Verbose, TEXT("[ComboTrace][Notify] OPEN | Animation=%s"), *GetNameSafe(Animation));
	SendComboWindowEvent(MeshComp, Animation, this, MyGameplayTags::DK_Event_ComboWindow_Open);
}

void UAnimNotifyState_ComboWindow::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
                                             const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);
	UE_LOG(LogTemp, Verbose, TEXT("[ComboTrace][Notify] CLOSE | Animation=%s"), *GetNameSafe(Animation));
	SendComboWindowEvent(MeshComp, Animation, this, MyGameplayTags::DK_Event_ComboWindow_Close);
}
