// Fill out your copyright notice in the Description page of Project Settings.


#include "Notifies/AnimNotifyState_ComboWindow.h"
#include "MyGameplayTags.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Abilities/GameplayAbilityTypes.h"

namespace
{
	// 作用：把动画时间点转换为发送给 Mesh Owner 的 GameplayEvent，供正在运行的 Ability 监听。
	void SendComboWindowEvent(USkeletalMeshComponent* MeshComp, const FGameplayTag& EventTag)
	{
		AActor* OwnerActor = MeshComp ? MeshComp->GetOwner() : nullptr;
		if (!OwnerActor)
		{
			return;
		}

		// Instigator/Target 都填写角色本身，因为这是“角色自己的动画窗口”事件，
		// 不是一次对敌人的命中。攻击 Ability 只关心 EventTag，但完整 Payload 更便于日志调试。
		FGameplayEventData EventData;
		EventData.Instigator = OwnerActor;
		EventData.Target = OwnerActor;

		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(OwnerActor,EventTag,EventData);
	}
}

void UAnimNotifyState_ComboWindow::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	UE_LOG(LogTemp,
        Warning,
        TEXT("我已经成功激活[ComboTrace][Notify] OPEN | Animation=%s"),
        *GetNameSafe(Animation)
    );
	SendComboWindowEvent(MeshComp, MyGameplayTags::DK_Event_ComboWindow_Open);
}

void UAnimNotifyState_ComboWindow::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
                                             const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);
	UE_LOG(
        LogTemp,
        Warning,
        TEXT("我已成功结束[ComboTrace][Notify] CLOSE | Animation=%s"),
        *GetNameSafe(Animation)
    );
	SendComboWindowEvent(MeshComp, MyGameplayTags::DK_Event_ComboWindow_Close);
}
