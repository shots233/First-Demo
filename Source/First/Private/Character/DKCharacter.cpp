// Fill out your copyright notice in the Description page of Project Settings.


#include "Character/DKCharacter.h"

#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "MyGameplayTags.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/Input/DKInputComponent.h"
#include "Components/Combat/DKCombatComponent.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "AbilitySystem/FirstAttributeSet.h"
#include "AbilitySystem/GameplayEffects/FirstGE_StaminaChange.h"
#include "Blueprint/UserWidget.h"
#include "Components/Combat/DKDefenseComponent.h"
#include "Components/Targeting/DKTargetLockComponent.h"
#include "Components/Locomotion/DKTurnInPlaceComponent.h"
#include "Components/UI/DKUIComponent.h"
#include "DataAssets/Input/DataAsset_InputConfig.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"

ADKCharacter::ADKCharacter()
{
	// CapsuleComponent 继承自 ACharacter，是角色碰撞和根组件。
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.f);

	// 镜头转动不直接带动角色旋转；角色朝向由移动方向决定。
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// 创建相机摇臂并挂到角色根组件（胶囊体）上。
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(GetRootComponent());
	CameraBoom->TargetArmLength = 350.f;
	CameraBoom->SocketOffset = FVector(0.f, 50.f, 60.f);
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bDoCollisionTest = true;

	// 相机挂到摇臂末端。旋转由 CameraBoom 继承 Controller 控制。
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	// CharacterMovement 已由 ACharacter 创建，这里只获取并配置，
	// 不要再次 CreateDefaultSubobject 一个移动组件。
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = true;
	Movement->RotationRate = FRotator(0.f, 600.f, 0.f);
	Movement->MaxWalkSpeed = WalkSpeed;
	Movement->BrakingDecelerationWalking = 1800.f;
	Movement->JumpZVelocity = 600.f;
	Movement->AirControl = 0.35f;
	Movement->GravityScale = 1.7f;

	DKCombatComponent = CreateDefaultSubobject<UDKCombatComponent>(TEXT("DKCombatComponent"));
	DKDefenseComponent =CreateDefaultSubobject<UDKDefenseComponent>(TEXT("DKDefenseComponent"));
	DKUIComponent = CreateDefaultSubobject<UDKUIComponent>(TEXT("DKUIComponent"));
	TargetLockComponent =CreateDefaultSubobject<UDKTargetLockComponent>(TEXT("TargetLockComponent"));
	TurnInPlaceComponent = CreateDefaultSubobject<UDKTurnInPlaceComponent>(TEXT("TurnInPlaceComponent"));
	JumpMaxCount = 1;
}

void ADKCharacter::SetControllerFacingOverride(UObject* Source, bool bEnabled)
{
	if (!Source) { return; }
	if (bEnabled) { ControllerFacingSources.Add(Source); }
	else { ControllerFacingSources.Remove(Source); }
	RefreshMovementRotationOverrides();
}

void ADKCharacter::SetAutomaticRotationSuppressed(UObject* Source, bool bSuppressed)
{
	if (!Source) { return; }
	if (bSuppressed) { RotationSuppressionSources.Add(Source); }
	else { RotationSuppressionSources.Remove(Source); }
	RefreshMovementRotationOverrides();
}

void ADKCharacter::RefreshMovementRotationOverrides()
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!Movement) { return; }
	const bool bHasRequest = !ControllerFacingSources.IsEmpty() || !RotationSuppressionSources.IsEmpty();
	if (bHasRequest && !bMovementRotationCached)
	{
		bCachedOrientRotationToMovement = Movement->bOrientRotationToMovement;
		bCachedUseControllerDesiredRotation = Movement->bUseControllerDesiredRotation;
		bMovementRotationCached = true;
	}
	if (bHasRequest)
	{
		Movement->bOrientRotationToMovement = false;
		Movement->bUseControllerDesiredRotation = RotationSuppressionSources.IsEmpty();
	}
	else if (bMovementRotationCached)
	{
		Movement->bOrientRotationToMovement = bCachedOrientRotationToMovement;
		Movement->bUseControllerDesiredRotation = bCachedUseControllerDesiredRotation;
		bMovementRotationCached = false;
	}
}

void ADKCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	//// 保留父类输入初始化。即使当前 BaseCharacter 没有绑定，也不应省略。
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	
	checkf(InputConfigDataAsset,
		TEXT("[%s] 没有配置 InputConfigDataAsset"), *GetNameSafe(this));
	
	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	check(PlayerController);
	
	ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer();
	check(LocalPlayer);
	
	// Mapping Context 属于本地玩家，而不是角色 Actor 本身。
	UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer);
	check(Subsystem);
	
	// 优先级 0 是基础输入环境；菜单、锁定或载具可使用更高优先级覆盖。
	Subsystem->AddMappingContext(InputConfigDataAsset->DefaultMappingContext,0);
	
	// 此 Cast 要求 Project Settings 中已将默认输入组件设为 DKInputComponent。
	UDKInputComponent* FirstInput = CastChecked<UDKInputComponent>(PlayerInputComponent);
	
	FirstInput->BindNativeInputAction(
	   InputConfigDataAsset,
	   MyGameplayTags::InputTag_Move,
	   ETriggerEvent::Triggered,
	   this,
	   &ThisClass::Input_Move);

	FirstInput->BindNativeInputAction(
		InputConfigDataAsset,
		MyGameplayTags::InputTag_Move,
		ETriggerEvent::Completed,
		this,
		&ThisClass::Input_MoveCompleted);
	FirstInput->BindNativeInputAction(
		InputConfigDataAsset,
		MyGameplayTags::InputTag_Move,
		ETriggerEvent::Canceled,
		this,
		&ThisClass::Input_MoveCompleted);

	FirstInput->BindNativeInputAction(
		InputConfigDataAsset,
		MyGameplayTags::InputTag_Look,
		ETriggerEvent::Triggered,
		this,
		&ThisClass::Input_Look);

	FirstInput->BindNativeInputAction(
		InputConfigDataAsset,
		MyGameplayTags::InputTag_Jump,
		ETriggerEvent::Started,
		this,
		&ThisClass::Input_JumpStarted);

	FirstInput->BindNativeInputAction(
		InputConfigDataAsset,
		MyGameplayTags::InputTag_Jump,
		ETriggerEvent::Completed,
		this,
		&ThisClass::Input_JumpCompleted);

	FirstInput->BindNativeInputAction(
		InputConfigDataAsset,
		MyGameplayTags::InputTag_ToggleSword,
		ETriggerEvent::Started,
		this,
		&ThisClass::Input_ToggleSword);
	
	FirstInput->BindAbilityInputActions(
		InputConfigDataAsset,
		this,
		&ThisClass::Input_AbilityInputPressed,
		&ThisClass::Input_AbilityInputReleased);
	
	FirstInput->BindNativeInputAction(
	InputConfigDataAsset,
	MyGameplayTags::InputTag_TargetLock,
	ETriggerEvent::Started,
	this,
	&ThisClass::Input_TargetLock);

	FirstInput->BindNativeInputAction(
		InputConfigDataAsset,
		MyGameplayTags::InputTag_SwitchTarget,
		ETriggerEvent::Triggered,
		this,
		&ThisClass::Input_SwitchTarget);
}

void ADKCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (FirstAbilitySystemComponent)
	{
		DodgeExhaustedChangedHandle = FirstAbilitySystemComponent->RegisterGameplayTagEvent(
			MyGameplayTags::DK_Status_DodgeExhausted, EGameplayTagEventType::NewOrRemoved)
			.AddUObject(this, &ThisClass::HandleDodgeExhaustedChanged);
		RunningStaminaChangedHandle = FirstAbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
			UFirstAttributeSet::GetStaminaAttribute()).AddUObject(this, &ThisClass::HandleRunningStaminaChanged);
	}
	OnCharacterMovementUpdated.AddUniqueDynamic(this, &ThisClass::HandleSprintMovementUpdated);
	RefreshRunningState();
	
	// 玩家 HUD：左下角血条/精力条。
	if (PlayerHUDWidgetClass)
	{
		if (UUserWidget* PlayerHUD = CreateWidget<UUserWidget>(GetWorld(), PlayerHUDWidgetClass))
		{
			PlayerHUD->AddToViewport();
		}
	}

	// BOSS HUD：顶部大血条（控件自己会去找场景里的 BOSS）。
	if (BossHUDWidgetClass)
	{
		if (UUserWidget* BossHUD = CreateWidget<UUserWidget>(GetWorld(), BossHUDWidgetClass))
		{
			BossHUD->AddToViewport();
		}
	}
}

void ADKCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	OnCharacterMovementUpdated.RemoveDynamic(this, &ThisClass::HandleSprintMovementUpdated);
	GetWorldTimerManager().ClearTimer(SprintRegenPauseTimerHandle);
	ReleaseSprintRegenPause();
	if (FirstAbilitySystemComponent && RunningStaminaChangedHandle.IsValid())
	{
		FirstAbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
			UFirstAttributeSet::GetStaminaAttribute()).Remove(RunningStaminaChangedHandle);
	}
	RunningStaminaChangedHandle.Reset();
	if (FirstAbilitySystemComponent && DodgeExhaustedChangedHandle.IsValid())
	{
		FirstAbilitySystemComponent->RegisterGameplayTagEvent(
			MyGameplayTags::DK_Status_DodgeExhausted, EGameplayTagEventType::NewOrRemoved)
			.Remove(DodgeExhaustedChangedHandle);
	}
	DodgeExhaustedChangedHandle.Reset();
	GetWorldTimerManager().ClearTimer(DodgeHoldRunTimerHandle);
	bDodgeInputHeld = false;
	bRunRequested = false;
	bIsRunning = false;
	Super::EndPlay(EndPlayReason);
}

void ADKCharacter::Input_Move(const FInputActionValue& Value)
{
	if (!Controller || IsHitReactOrDead())
	{
		if (TurnInPlaceComponent) { TurnInPlaceComponent->ReleaseMovementInput(); }
		return;
	}
	
	const FVector2D MovementVector = Value.Get<FVector2D>();
	// 只使用控制器 Yaw，避免镜头向上看时角色向天空移动。
	const FRotator MovementRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);
	
	const FVector ForwardDirection = MovementRotation.RotateVector(FVector::ForwardVector);
	const FVector RightDirection = MovementRotation.RotateVector(FVector::RightVector);
	const FVector WorldInput = ForwardDirection * MovementVector.Y + RightDirection * MovementVector.X;
	if (TurnInPlaceComponent && TurnInPlaceComponent->HandleMovementInput(WorldInput)) { return; }
	
	// Enhanced Input 的二维值约定：Y 前后，X 左右。
	AddMovementInput(ForwardDirection, MovementVector.Y);
	AddMovementInput(RightDirection, MovementVector.X);
	
}

void ADKCharacter::Input_MoveCompleted(const FInputActionValue& Value)
{
	if (TurnInPlaceComponent) { TurnInPlaceComponent->ReleaseMovementInput(); }
}

void ADKCharacter::Input_Look(const FInputActionValue& Value)
{
	// 锁定镜头由 TargetLockComponent 控制。
	// 若这里继续 AddControllerYaw/Pitch，会和自动镜头互相争抢。
	if (TargetLockComponent &&TargetLockComponent->IsTargetLocked())
	{
		return;
	}

	const FVector2D LookVector =Value.Get<FVector2D>();

	AddControllerYawInput(LookVector.X);
	AddControllerPitchInput(LookVector.Y);
	
}

void ADKCharacter::Input_JumpStarted(const FInputActionValue& Value)
{
	// 受击/死亡期间禁止起跳。
	if (IsHitReactOrDead())
	{
		return;
	}

	// 起跳物理由 CharacterMovement 负责。
	if (FirstAbilitySystemComponent &&
		(FirstAbilitySystemComponent->HasMatchingGameplayTag(
			MyGameplayTags::DK_Status_Defending) ||
		 FirstAbilitySystemComponent->HasMatchingGameplayTag(
			MyGameplayTags::DK_Status_GuardBroken) ||
		 FirstAbilitySystemComponent->HasMatchingGameplayTag(
			MyGameplayTags::DK_Status_Executing)))
	{
		return;
	}

	if (TurnInPlaceComponent) { TurnInPlaceComponent->InterruptTurn(); }
	Jump();
}

void ADKCharacter::Input_JumpCompleted(const FInputActionValue& Value)
{
	// 松开按键后停止继续施加跳跃保持时间，支持可变跳跃高度
	StopJumping();
}

void ADKCharacter::Input_TargetLock(const FInputActionValue& Value)
{
	// 受击/死亡期间禁止切换锁定模式。
	if (IsHitReactOrDead())
	{
		return;
	}

	// 保持现有输入规则：格挡/破防/处决期间不手动开关锁定模式。
	if (FirstAbilitySystemComponent &&
		(FirstAbilitySystemComponent->HasMatchingGameplayTag(
			MyGameplayTags::DK_Status_Defending) ||
		 FirstAbilitySystemComponent->HasMatchingGameplayTag(
			MyGameplayTags::DK_Status_GuardBroken) ||
		 FirstAbilitySystemComponent->HasMatchingGameplayTag(
			MyGameplayTags::DK_Status_Executing)))
	{
		return;
	}

	if (TargetLockComponent)
	{
		TargetLockComponent->ToggleTargetLock();
	}
}

void ADKCharacter::Input_SwitchTarget(const FInputActionValue& Value)
{
	// 受击/死亡期间禁止切换目标。
	if (IsHitReactOrDead())
	{
		return;
	}

	// 处决过程禁止切换目标。
	if (FirstAbilitySystemComponent &&
		FirstAbilitySystemComponent->HasMatchingGameplayTag(
			MyGameplayTags::DK_Status_Executing))
	{
		return;
	}

	if (TargetLockComponent)
	{
		TargetLockComponent->SwitchTarget(Value.Get<float>());
	}
}

// 作用：让同一个 1 键根据当前状态调用装备或卸下 Ability，而不是同时触发两者。
void ADKCharacter::Input_ToggleSword(const FInputActionValue& Value)
{
	// 受击/死亡期间禁止切换装备。
	if (IsHitReactOrDead())
	{
		return;
	}

	if (!DKCombatComponent)
	{
		return;
	}

	const bool bSwordEquipped =
		DKCombatComponent->CurrentEquippedWeaponTag == MyGameplayTags::DK_Weapon_Sword;
	const FGameplayTag AbilityInputTag = bSwordEquipped
		? MyGameplayTags::InputTag_UnequipSword
		: MyGameplayTags::InputTag_EquipSword;

	TriggerAbilityInputTap(AbilityInputTag);
}

// 作用：转发仍由 DataAsset 直接绑定的 Ability 输入；当前主要是鼠标左键轻攻击。
void ADKCharacter::Input_AbilityInputPressed(FGameplayTag InputTag)
{
	// 受击/死亡期间不启动任何 Ability 输入；
	// 必须在闪避长按计时器启动之前拦截。
	if (IsHitReactOrDead())
	{
		return;
	}

	// 弹反连锁也走下方 ASC 输入转发。GA 在 InputPressed 中保存提前输入，
	// 不在角色层按窗口标签过滤，否则窗口打开前的点按会丢失。

	// 闪避改为“按下立即触发 + 长按衔接奔跑”，不再由 Shift 手势生成。
	if (InputTag == MyGameplayTags::InputTag_Dodge)
	{
		// 记录按键仍被按住，供闪避结束后的奔跑判定使用。
		bDodgeInputHeld = true;
		
		// 提前进入奔跑的窗口，让闪避结尾和奔跑起始重叠。
		// 数值越小越接近原来的行为；数值越大，奔跑介入越早。
		const float DodgeHoldDuration = FMath::Max((GetDodgeMontage() ? GetDodgeMontage()->GetPlayLength() : GetDodgeDuration()) - DodgeRunOverlap,0.05f);
		
		GetWorldTimerManager().SetTimer(DodgeHoldRunTimerHandle,this,&ThisClass::HandleDodgeHoldElapsed,DodgeHoldDuration,false);
	}
	
	// 装备/卸下仍由 1 键切换函数产生；
	// 即使 DataAsset 暂时残留旧条目，也不允许它们绕过新的切换规则。
	
	if (InputTag == MyGameplayTags::InputTag_EquipSword ||
		InputTag == MyGameplayTags::InputTag_UnequipSword)
	{
		return;
	}

	if (FirstAbilitySystemComponent)
	{
		// 角色只转发 Tag。ASC 会找到匹配的已授予 Spec。
		// 闪避在这里被按下边沿立即激活，不再等待松开。
		FirstAbilitySystemComponent->OnAbilityInputPressed(InputTag);
	}
}

// 作用：把直接绑定 Ability 输入的松开边沿转发给 ASC，供等待松开类 AbilityTask 使用。
void ADKCharacter::Input_AbilityInputReleased(FGameplayTag InputTag)
{
	// 松开闪避键：结束“长按奔跑”的判定并恢复步行。
	if (InputTag == MyGameplayTags::InputTag_Dodge)
	{
		bDodgeInputHeld = false;
		GetWorldTimerManager().ClearTimer(DodgeHoldRunTimerHandle);
		SetRunning(false);
	}
	
	if (InputTag == MyGameplayTags::InputTag_EquipSword || InputTag == MyGameplayTags::InputTag_UnequipSword)
	{
		return;
	}

	if (FirstAbilitySystemComponent)
	{
		// Released 对长按技能和 AbilityTask_WaitInputRelease 有用；连击最小版本
		// 主要监听 Press，但仍应完整转发两个边沿事件。
		FirstAbilitySystemComponent->OnAbilityInputReleased(InputTag);
	}
	
}

void ADKCharacter::HandleDodgeHoldElapsed()
{
	if (bDodgeInputHeld)
	{
		SetRunning(true);
	}
}

FVector ADKCharacter::GetDodgeInputDirection() const
{
	FVector Direction = GetLastMovementInputVector();
	const APlayerController* PlayerController = Cast<APlayerController>(Controller);
	const UEnhancedPlayerInput* EnhancedPlayerInput = PlayerController
		? Cast<UEnhancedPlayerInput>(PlayerController->PlayerInput)
		: nullptr;
	const UInputAction* MoveAction = InputConfigDataAsset
		? InputConfigDataAsset->FindNativeInputActionByTag(MyGameplayTags::InputTag_Move, false)
		: nullptr;

	if (EnhancedPlayerInput && MoveAction)
	{
		// Started 会先于本帧移动的 Triggered 执行，LastMovementInput 可能仍是旧方向。
		// 直接读取本帧已计算好的动作值，同时保留重映射与手柄输入。
		const FVector2D MovementVector = EnhancedPlayerInput->GetActionValue(MoveAction).Get<FVector2D>();
		const FRotator MovementRotation(0.f, PlayerController->GetControlRotation().Yaw, 0.f);
		Direction = MovementRotation.RotateVector(FVector(MovementVector.Y, MovementVector.X, 0.f));
	}

	Direction.Z = 0.f;
	
	// 当前动作值为零也要默认向前，不能回退到已松开的旧方向。
	if (Direction.IsNearlyZero())
	{
		Direction = GetActorForwardVector();
		Direction.Z = 0.f;
	}
	
	Direction.Normalize();
	
	return Direction;
}

float ADKCharacter::GetDodgeReferenceYaw() const
{
	if (TargetLockComponent &&TargetLockComponent->IsTargetLocked())
	{
		FVector ToTarget =TargetLockComponent->GetCurrentTargetLocation() -GetActorLocation();
		ToTarget.Z = 0.f;

		if (!ToTarget.IsNearlyZero())
		{
			return ToTarget.Rotation().Yaw;
		}
	}

	return Super::GetDodgeReferenceYaw();
}

UPawnUIComponent* ADKCharacter::GetPawnUIComponent() const
{
	return DKUIComponent;
}

// 作用：向 ASC 连续发送一次 Pressed/Released，使状态判断函数能复用现有 AbilitySpec 输入路由。
void ADKCharacter::TriggerAbilityInputTap(const FGameplayTag& InputTag)
{
	if (!FirstAbilitySystemComponent || !InputTag.IsValid())
	{
		return;
	}

	FirstAbilitySystemComponent->OnAbilityInputPressed(InputTag);
	FirstAbilitySystemComponent->OnAbilityInputReleased(InputTag);
}

// 作用：统一保存奔跑状态并修改 CharacterMovement 的最大行走速度。
void ADKCharacter::SetRunning(bool bNewRunning)
{
	bRunRequested = bNewRunning;
	RefreshRunningState();
}

void ADKCharacter::HandleDodgeExhaustedChanged(const FGameplayTag Tag, int32 NewCount)
{
	// 进入锁立即停跑；解锁时只恢复仍然成立的长按请求，不重新派发闪避输入。
	RefreshRunningState();
}

float ADKCharacter::GetDesiredLocomotionSpeed() const
{
	// 直接查询状态，避免同一标签变化中其他 Ability 先恢复移动、读到旧缓存。
	return bRunRequested && CanRunWithCurrentStamina() ? RunSpeed : WalkSpeed;
}

bool ADKCharacter::CanRunWithCurrentStamina() const
{
	return FirstAbilitySystemComponent && FirstAttributeSet && FirstAttributeSet->GetStamina() > 0.f &&
		!FirstAbilitySystemComponent->HasMatchingGameplayTag(MyGameplayTags::DK_Status_DodgeExhausted);
}

void ADKCharacter::HandleRunningStaminaChanged(const FOnAttributeChangeData& Data)
{
	// 只在跨过零值时刷新资格，不在每一帧扣费时覆盖动作自己的移动速度。
	if ((Data.OldValue > 0.f) != (Data.NewValue > 0.f))
	{
		RefreshRunningState();
	}
}

void ADKCharacter::RefreshRunningState()
{
	bIsRunning = bRunRequested && CanRunWithCurrentStamina();

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!Movement)
	{
		return;
	}

	// 精力恢复事件也可能发生在动作期间，只更新请求结果，保留动作自己的速度。
	const bool bActionLocksMoveSpeed = FirstAbilitySystemComponent &&
		(FirstAbilitySystemComponent->HasMatchingGameplayTag(
			MyGameplayTags::DK_Status_Defending) ||
		 FirstAbilitySystemComponent->HasMatchingGameplayTag(
			MyGameplayTags::DK_Status_GuardBroken) ||
		 FirstAbilitySystemComponent->HasMatchingGameplayTag(
			MyGameplayTags::DK_Status_Executing) || IsHitReactOrDead());

	if (bActionLocksMoveSpeed)
	{
		return;
	}

	Movement->MaxWalkSpeed = GetDesiredLocomotionSpeed();
}

void ADKCharacter::HandleSprintMovementUpdated(float DeltaSeconds, FVector OldLocation, FVector OldVelocity)
{
	if (!HasAuthority() || !bIsRunning || !CanRunWithCurrentStamina() ||
		!FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.f ||
		!FMath::IsFinite(SprintStaminaCostPerSecond) || SprintStaminaCostPerSecond <= 0.f)
	{
		return;
	}

	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	// 有移动输入且真的在地面移动才计费；墙体阻挡、站立、空中和动作根位移不计入奔跑。
	// 忽略碰撞边缘的微小位置修正，阈值按速度计算，不使用依赖帧率的固定距离。
	constexpr float MinSprintMovementSpeed = 5.f;
	if (!Movement || !Movement->IsMovingOnGround() || IsPlayingRootMotion() || Movement->HasRootMotionSources() ||
		Movement->GetCurrentAcceleration().SizeSquared2D() <= UE_SMALL_NUMBER ||
		Movement->Velocity.SizeSquared2D() <= FMath::Square(MinSprintMovementSpeed) ||
		FVector::DistSquared2D(GetActorLocation(), OldLocation) <= FMath::Square(MinSprintMovementSpeed * DeltaSeconds))
	{
		return;
	}

	static const FGameplayTagContainer ActionTags = []
	{
		FGameplayTagContainer Tags;
		Tags.AddTag(MyGameplayTags::DK_Status_Dodging);
		Tags.AddTag(MyGameplayTags::DK_Status_Attacking);
		Tags.AddTag(MyGameplayTags::DK_Status_Defending);
		Tags.AddTag(MyGameplayTags::DK_Status_GuardBroken);
		Tags.AddTag(MyGameplayTags::DK_Status_Executing);
		Tags.AddTag(MyGameplayTags::DK_Status_ChangingWeapon);
		Tags.AddTag(MyGameplayTags::DK_Status_HitReact);
		Tags.AddTag(MyGameplayTags::Shared_Status_Dead);
		return Tags;
	}();
	if (FirstAbilitySystemComponent->HasAnyMatchingGameplayTags(ActionTags))
	{
		return;
	}

	FGameplayEffectSpecHandle CostSpec = FirstAbilitySystemComponent->MakeOutgoingSpec(
		UFirstGE_StaminaChange::StaticClass(), 1.f, FirstAbilitySystemComponent->MakeEffectContext());
	if (!CostSpec.IsValid())
	{
		return;
	}

	// 在扣费和本帧恢复周期执行之前暂停恢复，只增添自己拥有的一层。
	if (!bOwnsSprintRegenPause)
	{
		bOwnsSprintRegenPause = true;
		FirstAbilitySystemComponent->AddLooseGameplayTag(MyGameplayTags::DK_Status_StaminaRegenPaused);
	}
	const float RegenDelay = FMath::IsFinite(SprintStaminaRegenDelay)
		? FMath::Max(0.01f, SprintStaminaRegenDelay) : 0.5f;
	SprintRegenResumeTime = GetWorld()->GetTimeSeconds() + RegenDelay;
	// 每次实际跑动都更新释放期限。即使受击/处决 DisableMovement，不再有移动回调，也能按时恢复。
	GetWorldTimerManager().SetTimer(SprintRegenPauseTimerHandle, this,
		&ThisClass::TryReleaseSprintRegenPause, RegenDelay, false);

	const float Cost = FMath::Min(FirstAttributeSet->GetStamina(), SprintStaminaCostPerSecond * DeltaSeconds);
	CostSpec.Data->SetSetByCallerMagnitude(MyGameplayTags::Combat_SetByCaller_StaminaDelta, -Cost);
	// 走原有 GE 路径，保留精力夹紧、属性通知和精力条更新。
	FirstAbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*CostSpec.Data.Get());
	if (FirstAttributeSet->GetStamina() <= 0.f)
	{
		FirstAbilitySystemComponent->StartDodgeStaminaRecovery();
	}
}

void ADKCharacter::TryReleaseSprintRegenPause()
{
	// 移动更新先于世界 TimerManager；卡顿帧也必须从最后跑动时刻等满延迟。
	const double Remaining = SprintRegenResumeTime - GetWorld()->GetTimeSeconds();
	if (Remaining > UE_SMALL_NUMBER)
	{
		GetWorldTimerManager().SetTimer(SprintRegenPauseTimerHandle, this,
			&ThisClass::TryReleaseSprintRegenPause, static_cast<float>(Remaining), false);
		return;
	}
	ReleaseSprintRegenPause();
}

void ADKCharacter::ReleaseSprintRegenPause()
{
	if (bOwnsSprintRegenPause)
	{
		bOwnsSprintRegenPause = false;
		if (FirstAbilitySystemComponent)
		{
			FirstAbilitySystemComponent->RemoveLooseGameplayTag(MyGameplayTags::DK_Status_StaminaRegenPaused);
		}
	}
}

bool ADKCharacter::IsHitReactOrDead() const
{
	return FirstAbilitySystemComponent &&
		(FirstAbilitySystemComponent->HasMatchingGameplayTag(
			MyGameplayTags::DK_Status_HitReact) ||
		 FirstAbilitySystemComponent->HasMatchingGameplayTag(
			MyGameplayTags::Shared_Status_Dead));
}

void ADKCharacter::PrepareForDeath()
{
	StopJumping();

	GetWorldTimerManager().ClearTimer(DodgeHoldRunTimerHandle);
	bDodgeInputHeld = false;
	SetRunning(false);
	GetWorldTimerManager().ClearTimer(SprintRegenPauseTimerHandle);
	ReleaseSprintRegenPause();

	if (TargetLockComponent)
	{
		TargetLockComponent->ClearTargetLock();
	}
}

