#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "DKTurnInPlaceComponent.generated.h"

class ADKCharacter;
class AEnemyCharacter;
class UAnimInstance;
class UAnimMontage;
class URootMotionModifier;

// 低优先级移动表现：不占用 GAS 动作，不阻止任何战斗能力。
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class FIRST_API UDKTurnInPlaceComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UDKTurnInPlaceComponent();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// 返回 true 时由站立转身暂时接管本次移动输入；结束后仍按住的输入正常行走。
	bool HandleMovementInput(const FVector& WorldInput);
	void ReleaseMovementInput();
	// 跳跃、战斗动作或锁定在移动组件提取根运动前主动交还控制。
	void InterruptTurn();

	UFUNCTION(BlueprintPure, Category="Turn In Place")
	bool IsTurningInPlace() const { return ActiveMontage != nullptr; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend struct FFirstTurnInPlaceTestAccess;
#endif
	bool CanManageStandingRotation() const;
	bool HasBlockingAction() const;
	bool HasUsableAnimations() const;
	void UpdateTurn(float DeltaTime);
	void StartTurn(float YawDelta, float TargetYaw);
	void FinishTurn(bool bStopMontage);
	void SetRotationSuppressed(bool bSuppress);
	void HandleActionTagChanged(FGameplayTag Tag, int32 NewCount);
	void HandleMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted, int32 InstanceID);
	void HandleMontageEnded(UAnimMontage* Montage, bool bInterrupted, int32 InstanceID);
	UFUNCTION()
	void HandleTargetChanged(AEnemyCharacter* NewTarget);

	UPROPERTY(EditAnywhere, Category="Turn In Place")
	bool bEnabled = true;
	UPROPERTY(EditAnywhere, Category="Turn In Place|Animations")
	TObjectPtr<UAnimMontage> TurnLeft90;
	UPROPERTY(EditAnywhere, Category="Turn In Place|Animations")
	TObjectPtr<UAnimMontage> TurnRight90;
	UPROPERTY(EditAnywhere, Category="Turn In Place|Animations")
	TObjectPtr<UAnimMontage> TurnLeft180;
	UPROPERTY(EditAnywhere, Category="Turn In Place|Animations")
	TObjectPtr<UAnimMontage> TurnRight180;

	// 未锁定时，输入方向相对身体达到此角度，先转身再迈步。
	UPROPERTY(EditAnywhere, Category="Turn In Place|Settings", meta=(ClampMin="10.0", ClampMax="120.0", Units="deg"))
	float StartAngle = 60.f;
	UPROPERTY(EditAnywhere, Category="Turn In Place|Settings", meta=(ClampMin="90.0", ClampMax="180.0", Units="deg"))
	float LargeTurnAngle = 135.f;
	UPROPERTY(EditAnywhere, Category="Turn In Place|Settings", meta=(ClampMin="0.1", ClampMax="3.0"))
	float PlayRate = 1.5f;
	UPROPERTY(EditAnywhere, Category="Turn In Place|Settings", meta=(ClampMin="0.0", Units="cm/s"))
	float StationarySpeed = 3.f;
	UPROPERTY(EditAnywhere, Category="Turn In Place|Settings", meta=(ClampMin="0.0", Units="s"))
	float IdleDelay = 0.12f;
	UPROPERTY(EditAnywhere, Category="Turn In Place|Settings", meta=(ClampMin="0.0", Units="s"))
	float TurnCooldown = 0.15f;
	UPROPERTY(EditAnywhere, Category="Turn In Place|Settings", meta=(ClampMin="0.0", ClampMax="0.2", Units="s"))
	float InterruptBlendOutTime = 0.08f;

	UPROPERTY(Transient)
	TObjectPtr<ADKCharacter> Character;
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveMontage;
	TWeakObjectPtr<UAnimInstance> ActiveAnimInstance;
	TWeakObjectPtr<URootMotionModifier> ActiveModifier;
	FDelegateHandle ActionTagHandle;
	int32 ActiveMontageInstanceID = INDEX_NONE;
	float StationaryElapsed = 0.f;
	double NextTurnTime = 0.0;
	bool bRotationSuppressed = false;
};
