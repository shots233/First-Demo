// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "GameFramework/Character.h"
#include "Interfaces/PawnUIInterface.h"
#include "TimerManager.h"
#include "BaseCharacter.generated.h"

class UDataAsset_StartUpDataBase;
class UFirstAttributeSet;
class UFirstAbilitySystemComponent;
class UAbilitySystemComponent;
class UAnimMontage;
class UMotionWarpingComponent;
class URootMotionModifier_Warp;
struct FFirstAttackWarpingData;

UCLASS()
class FIRST_API ABaseCharacter : public ACharacter, public IAbilitySystemInterface, public IPawnUIInterface
{
	GENERATED_BODY()

public:
	ABaseCharacter();

	// 作用：实现 GAS 查询接口，使通用函数能从角色 Actor 找到它的 ASC。
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	// 作用：实现 PawnUIInterface 接口，提供对 PawnUIComponent 的访问。
	virtual UPawnUIComponent* GetPawnUIComponent() const override;

protected:
	
	// 作用：角色被控制器占有时初始化 ASC 的 Owner/Avatar 上下文，并授予出生数据。
	virtual void PossessedBy(AController* NewController) override;
	// 作用：客户端同步到 PlayerState 后再次确保 ASC 上下文有效，为以后多人扩展保留入口。
	virtual void OnRep_PlayerState() override;
	// 作用：保留 ACharacter 的输入设置链；具体 Ability 输入绑定由子类完成。
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	
	// 两个函数分开：先建立 Owner/Avatar 上下文，再应用初始属性和授予 Ability。
	// 先后顺序不能反，因为 GiveAbility / GE 都需要有效的 ActorInfo。
	// 作用：建立 ASC 与当前角色的 ActorInfo 关系，后续 Ability 和 Effect 才知道作用对象是谁。
	void InitializeAbilitySystem();
	// 作用：读取角色数据资产，应用初始 GameplayEffect 并授予出生需要的 Ability。
	void GiveStartupData();
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AbilitySystem")
	TObjectPtr<UFirstAbilitySystemComponent> FirstAbilitySystemComponent;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AbilitySystem")
	TObjectPtr<UFirstAttributeSet> FirstAttributeSet;

	// 近战吸附：攻击蒙太奇的根位移通过该组件对齐到 WarpTarget（目标角色胸口）。
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat",
		meta=(AllowPrivateAccess="true"))
	TObjectPtr<UMotionWarpingComponent> MotionWarpingComponent;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Character Data")
	TSoftObjectPtr<UDataAsset_StartUpDataBase> CharacterStartUpData;
	
private:
	// 只在攻击 Ability 存活期间保留；Ability 结束时必须调用 EndAttackWarping。
	TWeakObjectPtr<ACharacter> AttackWarpTargetActor;
	FTimerHandle AttackWarpUpdateTimerHandle;
	float AttackWarpMaxDistance = 0.f;
	float AttackWarpSurfaceGap = 0.f;
	float AttackWarpMaxTravelDistance = 0.f;
	bool bKeepAttackWarpTargetWhenBeyondMaxDistance = false;
	float AttackFacingTurnRate = 0.f;
	float AttackMaxFacingAngle = 0.f;
	float AttackFacingOriginYaw = 0.f;

	// 计算止步点；动态模式会重复调用，固定快照模式只在会话开始时调用一次。
	void UpdateAttackWarping();
	const URootMotionModifier_Warp* FindActiveAttackWarpModifier() const;
	bool IsAttackWarpTargetUsable(const ACharacter* Target) const;

	// PossessedBy 与 OnRep_PlayerState 都可能调用初始化；这个标记防止重复授予 Ability。
	bool bStartupDataGiven = false;
	
	// 兜底向前闪避蒙太奇。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Dodge", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UAnimMontage> DodgeMontage;
	
	// 8 个方向的闪避蒙太奇，顺序固定：
	// 0=前 1=前右 2=右 3=后右 4=后 5=后左 6=左 7=前左。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Dodge", meta=(AllowPrivateAccess="true"))
	TArray<TObjectPtr<UAnimMontage>> DodgeMontages;

	// 无根运动时的位移强度。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Dodge", meta=(AllowPrivateAccess="true"))
	float DodgeImpulse = 650.f;
	
	// 没有蒙太奇时的占位持续时间。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Dodge", meta=(AllowPrivateAccess="true"))
	float DodgeDuration = 0.35f;
	
	// 是否由动画根运动提供位移。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Dodge", meta=(AllowPrivateAccess="true"))
	bool bDodgeUsesRootMotion = false;
	
public:
	
	// 作用：为 C++ 或蓝图派生类提供本项目类型的 ASC 快捷访问入口。
	FORCEINLINE UFirstAbilitySystemComponent* GetFirstAbilitySystemComponent() const
	{
		return FirstAbilitySystemComponent;
	}

	// 作用：为角色逻辑提供本项目 AttributeSet 的快捷访问入口，避免重复 Cast。
	FORCEINLINE UFirstAttributeSet* GetFirstAttributeSet() const
	{
		return FirstAttributeSet;
	}

	// 作用：供攻击能力设置 WarpTarget（近战吸附）。
	FORCEINLINE UMotionWarpingComponent* GetMotionWarpingComponent() const
	{
		return MotionWarpingComponent;
	}

	// 为一段攻击建立唯一的 AttackTarget。调用时会先清除上一段遗留目标。
	void BeginAttackWarping(ACharacter* Target, const FFirstAttackWarpingData& WarpingData);

	// 清理计时器和 AttackTarget；正常结束、取消、受击与死亡路径都应调用。
	void EndAttackWarping();
	
	// —— 闪避通用接口（玩家与 BOSS 共用）——
	
	// 方向来源：玩家=按键方向，BOSS=以后由 AI 决定。可覆写。
	virtual FVector GetDodgeInputDirection() const;
	
	// 方向基准 Yaw：玩家=镜头；BOSS 以后可覆写为目标方向。
	virtual float GetDodgeReferenceYaw() const;

	// 计算 8 向索引（0～7）。
	int32 GetDodgeDirectionIndex() const;
	
	// 纯函数：任意方向向量 + 基准Yaw → 8向索引。
	static int32 QuantizeDodgeDirection(const FVector& InDirection, float ReferenceYaw);
	
	// 按方向索引取闪避蒙太奇；越界或未配置时回退到 DodgeMontage。
	UAnimMontage* GetDodgeMontageForDirection(int32 DirectionIndex) const;

	FORCEINLINE UAnimMontage* GetDodgeMontage() const { return DodgeMontage; }
	FORCEINLINE float GetDodgeImpulse() const { return DodgeImpulse; }
	FORCEINLINE float GetDodgeDuration() const { return DodgeDuration; }
	FORCEINLINE bool DoesDodgeUseRootMotion() const { return bDodgeUsesRootMotion; }
	
};
