#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "ANS_WeaponTelegraph.generated.h"

/**
 * 武器白光预警窗口。
 * 进入窗口时开启当前武器的独立预警特效并播放一次可选提示音，离开时关闭。
 * 本通知不控制刀光拖影或伤害碰撞。
 */
UCLASS(meta = (DisplayName = "Weapon Telegraph"))
class FIRST_API UANS_WeaponTelegraph : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		float TotalDuration, const FAnimNotifyEventReference& EventReference) override;

	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
};
