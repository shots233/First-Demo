// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FirstWeaponBase.generated.h"


class UBoxComponent;
class UStaticMeshComponent;
class UBoxComponent;
class UStaticMeshComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class USoundBase;

DECLARE_DELEGATE_OneParam(FOnTargetInteractedDelegate, AActor*);

UCLASS()
class FIRST_API AFirstWeaponBase : public AActor
{
	GENERATED_BODY()
	
public:	
	AFirstWeaponBase();

	FOnTargetInteractedDelegate OnWeaponHitTarget;
	FOnTargetInteractedDelegate OnWeaponPulledFromTarget;
	
protected:

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Weapon")
	TObjectPtr<UStaticMeshComponent> WeaponMesh;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Weapon")
	TObjectPtr<UBoxComponent> WeaponCollisionBox;
	
	// 刀光拖影的 Niagara 模板；在 BP_DKWeapon_Sword 中统一配置，换风格只改这里。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Weapon|VFX",meta=(AllowPrivateAccess="true"))
	TObjectPtr<UNiagaraSystem> SlashTrailTemplate;

	// 拖影组件：默认停用，由蒙太奇通知在挥剑期间开关。
	// 挂到 WeaponMesh，跟随刀身运动生成 Ribbon 拖影。
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Weapon|VFX",meta=(AllowPrivateAccess="true"))
	TObjectPtr<UNiagaraComponent> SlashTrailComponent;

	// 攻击承诺点使用的独立白光预警；不复用刀光拖影，方便分别控制时序和资源。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Weapon|VFX|Telegraph",meta=(AllowPrivateAccess="true"))
	TObjectPtr<UNiagaraSystem> WeaponTelegraphTemplate;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Weapon|VFX|Telegraph",meta=(AllowPrivateAccess="true"))
	TObjectPtr<UNiagaraComponent> WeaponTelegraphComponent;

	// 可选：预警窗口首次开启时播放一次，不会因重复开启调用而重复播放。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Weapon|VFX|Telegraph",meta=(AllowPrivateAccess="true"))
	TObjectPtr<USoundBase> WeaponTelegraphSound;

	bool bWeaponTelegraphActive = false;

	// 作用：碰撞盒刚接触 Pawn 时过滤自身，再通过委托通知 CombatComponent 有目标被命中。
	UFUNCTION()
	virtual void OnCollisionBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
	
	// 作用：碰撞盒离开 Pawn 时过滤自身，再通过委托通知 CombatComponent 目标已离开。
	UFUNCTION()
	virtual void OnCollisionBoxEndOverlap(UPrimitiveComponent* OverlappedComponent,AActor* OtherActor,
		UPrimitiveComponent* OtherComp,int32 OtherBodyIndex);
	
public:	

	// 作用：判断目标是否"确定已死亡"（ASC 带 Shared_Status_Dead 标签）。
	// 无 ASC 的目标按存活处理：与 ABaseCharacter::IsAttackWarpTargetUsable 同口径，
	// 命中链入口只拦截明确死亡的尸体，其余交给结算层兜底。
	static bool IsTargetDead(const AActor* Target);

	// 作用：把武器碰撞盒暴露给 CombatComponent，用于由动画通知控制开启与关闭。
	FORCEINLINE UBoxComponent* GetWeaponCollisionBox() const
	{
		return WeaponCollisionBox;
	}

	// 作用：开关刀光拖影。启用时切换/激活 SlashTrailTemplate；停用时立即关闭。
	UFUNCTION(BlueprintCallable, Category="Weapon|VFX")
	void SetSlashTrailEnabled(bool bShouldEnable);

	// 开关独立武器预警。重复开启是幂等的；关闭可由 Notify End 与 Ability 清理共同兜底。
	UFUNCTION(BlueprintCallable, Category="Weapon|VFX|Telegraph")
	void SetWeaponTelegraphEnabled(bool bShouldEnable);
};
