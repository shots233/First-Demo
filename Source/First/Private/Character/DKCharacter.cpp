// Fill out your copyright notice in the Description page of Project Settings.


#include "Character/DKCharacter.h"

#include "EnhancedInputSubsystems.h"
#include "MyGameplayTags.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/Input/DKInputComponent.h"
#include "Components/Combat/DKCombatComponent.h"
#include "AbilitySystem/FirstAbilitySystemComponent.h"
#include "DataAssets/Input/DataAsset_InputConfig.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

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
	
	
	JumpMaxCount = 1;
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
}

void ADKCharacter::Input_Move(const FInputActionValue& Value)
{
	if (!Controller)
	{
		return;
	}
	
	const FVector2D MovementVector = Value.Get<FVector2D>();
	// 只使用控制器 Yaw，避免镜头向上看时角色向天空移动。
	const FRotator MovementRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);
	
	const FVector ForwardDirection = MovementRotation.RotateVector(FVector::ForwardVector);
	const FVector RightDirection = MovementRotation.RotateVector(FVector::RightVector);
	
	// Enhanced Input 的二维值约定：Y 前后，X 左右。
	AddMovementInput(ForwardDirection, MovementVector.Y);
	AddMovementInput(RightDirection, MovementVector.X);
	
}

void ADKCharacter::Input_Look(const FInputActionValue& Value)
{
	const FVector2D LookVector = Value.Get<FVector2D>();
	AddControllerYawInput(LookVector.X);
	AddControllerPitchInput(LookVector.Y);
	
}

void ADKCharacter::Input_JumpStarted(const FInputActionValue& Value)
{
	// 起跳物理由 CharacterMovement 负责。
	Jump();
}

void ADKCharacter::Input_JumpCompleted(const FInputActionValue& Value)
{
	// 松开按键后停止继续施加跳跃保持时间，支持可变跳跃高度
	StopJumping();
}

// 作用：让同一个 1 键根据当前状态调用装备或卸下 Ability，而不是同时触发两者。
void ADKCharacter::Input_ToggleSword(const FInputActionValue& Value)
{
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
	// 闪避改为“按下立即触发 + 长按衔接奔跑”，不再由 Shift 手势生成。
	if (InputTag == MyGameplayTags::InputTag_Dodge)
	{
		// 记录按键仍被按住，供闪避结束后的奔跑判定使用。
		bDodgeInputHeld = true;
		
		// 提前进入奔跑的窗口，让闪避结尾和奔跑起始重叠。
		// 数值越小越接近原来的行为；数值越大，奔跑介入越早。
		const float DodgeHoldDuration = FMath::Max((DodgeMontage ? DodgeMontage->GetPlayLength() : GetDodgeDuration()) - DodgeRunOverlap,0.05f);
		
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
	bIsRunning = bNewRunning;

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!Movement)
	{
		return;
	}

	// 真正影响移动速度的是 CharacterMovement 的 MaxWalkSpeed。
	// 动画蓝图只读取速度，不直接决定角色能跑多快。
	Movement->MaxWalkSpeed = bIsRunning ? RunSpeed : WalkSpeed;
}

