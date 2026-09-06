# 玩家与 BOSS 血条 UI 实现指导

> 适用项目：`First`（UE 5.6 **中文版** / C++ / GAS / UMG）
> 前置条件：主角（DK）与 BOSS 都已能正常运行；玩家属性 Health/MaxHealth/Stamina/MaxStamina 已生效
> 本册目标：左下角玩家血条 + 精力条；顶部居中 BOSS 大血条 + BOSS 名字 + 韧性条（实时）

---

## 0. 先看结论（本册完成后得到什么）

```text
左下角（玩家 HUD）
  ┌─ 血条（红）──────────┐
  └─ 精力条（青/绿）──────┘

顶部居中（BOSS HUD，横跨屏幕上方，很大）
  ┌──────────── 古龙骑士 · 格里芬 ────────────┐
  ┌──────────────── 血条（红，大） ────────────────┐
  ┌────────── 韧性条（银白，细） ────────────────┐
```

数据流（不 Tick 轮询）：

```text
GameplayEffect 修改属性
→ UFirstAttributeSet::PostGameplayEffectExecute 夹紧
→ 通过 IPawnUIInterface 找到 UI 组件
→ 广播 OnCurrentHealthChanged / OnCurrentStaminaChanged / OnCurrentPoiseChanged
→ UMG 控件（C++ 基类已绑定）更新 ProgressBar
```

---

## 1. 布局设计建议

### 1.1 玩家 HUD（左下角）

| 元素 | 建议数值 |
|---|---|
| 血条 | 宽 500、高 24，红色 |
| 精力条 | 宽 500、高 16，青色，位于血条下方 8px |
| 锚点 | 左下角（0,0），相对屏幕偏移 (40, 40) |
| 安全区 | 整个面板外面包一层**安全区（Safe Area）** |

### 1.2 BOSS HUD（顶部居中，横条）

| 元素 | 建议数值 |
|---|---|
| BOSS 名字 | 血条上方居中，字号 40，白/金色 |
| 血条 | 宽 1200、高 36，红色，锚点顶部居中（0.5,0），向下偏移 40 |
| 韧性条 | 宽 1200、高 10，银白色，血条下方 8px |
| 血条描边 | 深色底衬/描边，保证亮场景可读 |

> 宽度也可以不写死：用**视口比例**（如视口宽度的 70%），以后换分辨率自动适配。本册先用固定像素，简单直观。

---

## 2. 本册改动总览（按文件找位置）

| 文件/资产 | 路径 | 操作 | 新增/修改内容 |
|---|---|---|---|
| `First.Build.cs` | `Source/First/` | 修改 | 加 `UMG` 模块 |
| `PawnUIComponent.h/.cpp` | `Public/Components/UI/`<br>`Private/Components/UI/` | 新建 | 三个百分比委托（血量/精力/韧性） |
| `DKUIComponent.h/.cpp` | 同上 | 新建 | 玩家 UI 组件（本册为空壳，以后扩展） |
| `EnemyUIComponent.h/.cpp` | 同上 | 新建 | BOSS UI 组件（本册为空壳，以后扩展） |
| `PawnUIInterface.h/.cpp` | `Public/Interfaces/`<br>`Private/Interfaces/` | 新建 | 组件访问接口 |
| `BaseCharacter.h` | `Public/Character/` | 修改 | 实现 `IPawnUIInterface`（默认返回 nullptr） |
| `DKCharacter.h/.cpp` | `Public/Character/`<br>`Private/Character/` | 修改 | 创建 `UDKUIComponent`；新增两个 `TSubclassOf<UUserWidget>` HUD 类引用与 `BeginPlay` 创建显示（不直接依赖控件基类） |
| `BossCharacter.h/.cpp` | `Public/Character/`<br>`Private/Character/` | 修改 | 创建 `UEnemyUIComponent`；新增 `BossName` 属性 |
| `FirstAttributeSet.h/.cpp` | `Public/AbilitySystem/`<br>`Private/AbilitySystem/` | 修改 | 新增 Poise/MaxPoise；广播三个百分比 |
| `UFirstPlayerHUDWidget.h/.cpp` | `Public/UI/`<br>`Private/UI/` | 新建 | 玩家 HUD 控件基类（绑定委托、更新进度条） |
| `UFirstBossHUDWidget.h/.cpp` | 同上 | 新建 | BOSS HUD 控件基类（找 BOSS、绑定委托、设置名字） |
| `WBP_PlayerHUD` | `/Game/UI/` | 新建 | 左下角布局（血条/精力条） |
| `WBP_BossHUD` | `/Game/UI/` | 新建 | 顶部布局（名字/血条/韧性条） |
| `GE_Boss_Initialize` | `/Game/Enemy/GAS/Effects/` | 修改 | 加 MaxPoise/Poise 两个修饰符（100） |
| `BP_DKCharacter` | `/Game/DKCharacter/` | 修改 | 填 PlayerHUDWidgetClass / BossHUDWidgetClass |
| `BP_Boss` | `/Game/Enemy/` | 修改 | 填 BossName |

> 命名说明：组件和接口沿用 Warrior 参考项目的名字（`UPawnUIComponent` 等）；控件基类用项目级 `First` 前缀。

---

## 3. 第一步：模块依赖

> 【改动位置】文件 `First.Build.cs` 的 `PublicDependencyModuleNames`
> 【修改内容】新增 `UMG`

```csharp
		PublicDependencyModuleNames.AddRange(new string[] { "Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"GameplayTags",
			"GameplayAbilities",
			"GameplayTasks",
			"AIModule",
			"NavigationSystem",
			"UMG" });
```

改完做一次普通 Build（闸门 1）。

---

## 4. 第二步：UI 组件与接口（新建 4 组文件）

### 4.1 UPawnUIComponent（三个百分比委托）

用编辑器向导创建（**父类选择不能省略**）：

1. **工具（Tools）→ 新建 C++ 类（New C++ Class）** → 勾选 **显示所有类（Show All Classes）**；
2. 搜索并选择父类 **`ActorComponent`**（即 `UActorComponent`）；
3. 类名填写 `PawnUIComponent`（**不要带 U 前缀**，向导会自动加 U，最终类名 `UPawnUIComponent`）；
4. 创建后把下面的内容整体替换进去（新建文件，内容全部为新增）。

> 【改动位置】新建文件 `PawnUIComponent.h` / `PawnUIComponent.cpp`
> 【新增内容】三个动态多播委托：血量 / 精力 / 韧性

`Source/First/Public/Components/UI/PawnUIComponent.h`：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PawnUIComponent.generated.h"

// 属性变化时广播的百分比（0~1）。
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPercentChangedDelegate, float, NewPercent);

/**
 * 所有角色共用的 UI 数据组件。
 * AttributeSet 夹紧属性后，通过它广播百分比，UMG 控件据此更新进度条。
 */
UCLASS()
class FIRST_API UPawnUIComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category="UI")
	FOnPercentChangedDelegate OnCurrentHealthChanged;

	UPROPERTY(BlueprintAssignable, Category="UI")
	FOnPercentChangedDelegate OnCurrentStaminaChanged;

	// BOSS 韧性（Phase 3 前保持满值，先让 UI 链路跑通）。
	UPROPERTY(BlueprintAssignable, Category="UI")
	FOnPercentChangedDelegate OnCurrentPoiseChanged;
};
```

`Source/First/Private/Components/UI/PawnUIComponent.cpp`：

```cpp
#include "Components/UI/PawnUIComponent.h"
```

### 4.2 UDKUIComponent（玩家专属，本册空壳）

用编辑器向导创建：

1. **工具（Tools）→ 新建 C++ 类（New C++ Class）** → 勾选 **显示所有类（Show All Classes）**；
2. 搜索并选择父类 **`PawnUIComponent`**（即 `UPawnUIComponent`，**必须先完成 4.1 并编译通过**）；
3. 类名填写 `DKUIComponent`（不要带 U 前缀，向导会自动加 U，最终类名 `UDKUIComponent`）；
4. 创建后整体替换为下面的内容（新建文件）。

> 【改动位置】新建文件 `DKUIComponent.h` / `DKUIComponent.cpp`
> 【新增内容】继承 `UPawnUIComponent`，以后放玩家专属 UI 逻辑（连击计数、格挡提示等）

`Source/First/Public/Components/UI/DKUIComponent.h`：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Components/UI/PawnUIComponent.h"
#include "DKUIComponent.generated.h"

/**
 * 玩家侧 UI 组件。本册为空壳，后续扩展连击提示、格挡状态等。
 */
UCLASS()
class FIRST_API UDKUIComponent : public UPawnUIComponent
{
	GENERATED_BODY()
};
```

`Source/First/Private/Components/UI/DKUIComponent.cpp`：

```cpp
#include "Components/UI/DKUIComponent.h"
```

### 4.3 UEnemyUIComponent（BOSS 专属，本册空壳）

用编辑器向导创建：

1. **工具（Tools）→ 新建 C++ 类（New C++ Class）** → 勾选 **显示所有类（Show All Classes）**；
2. 搜索并选择父类 **`PawnUIComponent`**（即 `UPawnUIComponent`）；
3. 类名填写 `EnemyUIComponent`（不要带 U 前缀，向导会自动加 U，最终类名 `UEnemyUIComponent`）；
4. 创建后整体替换为下面的内容（新建文件）。

> 【改动位置】新建文件 `EnemyUIComponent.h` / `EnemyUIComponent.cpp`
> 【新增内容】继承 `UPawnUIComponent`，以后放 BOSS 血条显隐、处决提示等

`Source/First/Public/Components/UI/EnemyUIComponent.h`：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Components/UI/PawnUIComponent.h"
#include "EnemyUIComponent.generated.h"

/**
 * BOSS 侧 UI 组件。本册为空壳，后续扩展血条淡入、处决特效等。
 */
UCLASS()
class FIRST_API UEnemyUIComponent : public UPawnUIComponent
{
	GENERATED_BODY()
};
```

`Source/First/Private/Components/UI/EnemyUIComponent.cpp`：

```cpp
#include "Components/UI/EnemyUIComponent.h"
```

### 4.4 IPawnUIInterface（组件访问接口）

用编辑器向导创建：

1. **工具（Tools）→ 新建 C++ 类（New C++ Class）** → 勾选 **显示所有类（Show All Classes）**；
2. 搜索并选择父类 **`Interface`**（选择 `UInterface`）；
3. 类名填写 `PawnUIInterface`（向导会自动生成 `UPawnUIInterface` 与 `IPawnUIInterface` 一对）；
4. 创建后整体替换为下面的内容（新建文件）。

> 【改动位置】新建文件 `PawnUIInterface.h` / `PawnUIInterface.cpp`
> 【新增内容】统一入口：AttributeSet 通过它拿到角色身上的 UI 组件

`Source/First/Public/Interfaces/PawnUIInterface.h`：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "PawnUIInterface.generated.h"

class UPawnUIComponent;

UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UPawnUIInterface : public UInterface
{
	GENERATED_BODY()
};

class FIRST_API IPawnUIInterface
{
	GENERATED_BODY()

public:
	virtual UPawnUIComponent* GetPawnUIComponent() const = 0;
};
```

`Source/First/Private/Interfaces/PawnUIInterface.cpp`：

```cpp
#include "Interfaces/PawnUIInterface.h"
```

编译闸门 2：以上 4 组文件能编译通过。

---

## 5. 第三步：角色接入 UI 组件

### 5.1 ABaseCharacter 实现接口（默认返回 nullptr）

> 【改动位置】文件 `BaseCharacter.h`
> 【修改内容】类声明追加 `public IPawnUIInterface`；新增 `GetPawnUIComponent()` 覆写

`BaseCharacter.h` 的 include 区新增：

```cpp
#include "Interfaces/PawnUIInterface.h"
```

类声明改为：

```cpp
class FIRST_API ABaseCharacter : public ACharacter, public IAbilitySystemInterface, public IPawnUIInterface
```

`public:` 区域新增：

```cpp
	// 默认没有 UI 组件；玩家与 BOSS 各自覆写，返回自己的组件。
	virtual UPawnUIComponent* GetPawnUIComponent() const override;
```

> 【改动位置】文件 `BaseCharacter.cpp`
> 【新增内容】`GetPawnUIComponent()` 默认实现

```cpp
#include "Components/UI/PawnUIComponent.h"
```

```cpp
UPawnUIComponent* ABaseCharacter::GetPawnUIComponent() const
{
	return nullptr;
}
```

### 5.2 ADKCharacter 创建玩家 UI 组件

> 【改动位置】文件 `DKCharacter.h`
> 【新增内容】`DKUIComponent` 成员；两个 HUD 类引用；`BeginPlay()` 与 `GetPawnUIComponent()` 覆写声明；`GetDKUIComponent()` 访问器

头文件顶部还需要**前置声明**两个类型（放在现有 `class ADKWeapon;` 那一片）：

```cpp
class UDKUIComponent;
class UUserWidget;
```

> 为什么 HUD 类引用用 `TSubclassOf<UUserWidget>` 而不是具体的 `UFirstPlayerHUDWidget`：
> 控件基类要到后面第 7 步才创建，如果角色头文件现在就引用它，就会出现"类还没创建就先被使用"的顺序问题。
> 用引擎自带的 `UUserWidget` 做类型，角色不依赖控件基类，顺序自然成立；运行时用 `CreateWidget<UUserWidget>` 创建出蓝图控件，控件自己会在 `NativeConstruct` 里绑定数据。

`DKCharacter.h` 的 `public:` 区域（`GetDKCombatComponent` 附近）新增：

```cpp
	// 玩家 UI 数据组件（血量/精力广播）。
	FORCEINLINE UDKUIComponent* GetDKUIComponent() const { return DKUIComponent; }

	// IPawnUIInterface：返回玩家 UI 组件。
	virtual UPawnUIComponent* GetPawnUIComponent() const override;
```

`protected:` 区域新增：

```cpp
	// 创建并显示玩家 HUD 与 BOSS HUD。
	virtual void BeginPlay() override;
```

`private:` 区域新增：

```cpp
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="UI", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UDKUIComponent> DKUIComponent;

	// 在 BP_DKCharacter 类默认值中配置：两个 HUD 控件类（用引擎基类，避免依赖尚未创建的控件基类）。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="UI|HUD", meta=(AllowPrivateAccess="true"))
	TSubclassOf<UUserWidget> PlayerHUDWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="UI|HUD", meta=(AllowPrivateAccess="true"))
	TSubclassOf<UUserWidget> BossHUDWidgetClass;
```

> 【改动位置】文件 `DKCharacter.cpp`
> 【新增内容】构造函数创建组件；`BeginPlay()` 创建并显示两个 HUD；`GetPawnUIComponent()` 实现

构造函数里（`DKCombatComponent` 创建之后）新增：

```cpp
	DKUIComponent = CreateDefaultSubobject<UDKUIComponent>(TEXT("DKUIComponent"));
```

新增 include：

```cpp
#include "Blueprint/UserWidget.h"
#include "Components/UI/DKUIComponent.h"
```

新增两个函数：

```cpp
void ADKCharacter::BeginPlay()
{
	Super::BeginPlay();

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

UPawnUIComponent* ADKCharacter::GetPawnUIComponent() const
{
	return DKUIComponent;
}
```

> ⚠️ 为什么第一个参数是 `GetWorld()` 而不是 `this`：`CreateWidget` 的拥有者（OwningObject）
> 只支持 `UWorld`、控件（`UWidget`）或玩家控制器（`APlayerController`），**不支持任意 Actor**。
> 角色是 Actor，直接传 `this` 会触发 static_assert（C2338）。角色在 `BeginPlay` 里用 `GetWorld()`
> 创建即可；想更"标准"一点也可以先取 `GetWorld()->GetFirstPlayerController()` 再传给它。

### 5.3 ABossCharacter 创建 BOSS UI 组件 + BossName

> 【改动位置】文件 `BossCharacter.h`
> 【新增内容】`EnemyUIComponent` 成员、`BossName` 属性、`GetPawnUIComponent()` 覆写与 `GetEnemyUIComponent()` 访问器

头文件顶部新增前置声明：

```cpp
class UEnemyUIComponent;
```

`BossCharacter.h` 的 `public:` 区域新增：

```cpp
	// 顶部 BOSS 血条显示的名字（在 BP_Boss 类默认值里填）。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|UI", meta=(AllowPrivateAccess="true"))
	FText BossName;

	// IPawnUIInterface：返回 BOSS UI 组件。
	virtual UPawnUIComponent* GetPawnUIComponent() const override;

	FORCEINLINE UEnemyUIComponent* GetEnemyUIComponent() const { return EnemyUIComponent; }
```

`private:` 区域新增：

```cpp
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="UI", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UEnemyUIComponent> EnemyUIComponent;
```

> 【改动位置】文件 `BossCharacter.cpp`
> 【新增内容】构造函数创建组件；`GetPawnUIComponent()` 实现

构造函数里新增：

```cpp
	EnemyUIComponent = CreateDefaultSubobject<UEnemyUIComponent>(TEXT("EnemyUIComponent"));
```

新增 include：

```cpp
#include "Components/UI/EnemyUIComponent.h"
```

新增函数：

```cpp
UPawnUIComponent* ABossCharacter::GetPawnUIComponent() const
{
	return EnemyUIComponent;
}
```

编译闸门 3：角色接入完成，能编译。

---

## 6. 第四步：AttributeSet 广播 + 新增 Poise 属性

### 6.1 头文件新增 Poise / MaxPoise

> 【改动位置】文件 `FirstAttributeSet.h`
> 【新增内容】`Poise`、`MaxPoise` 两个属性（决策书 A3～A7 的韧性基础；Phase 3 再实现削减逻辑）

在 `DamageTaken` 属性下方新增：

```cpp
	// BOSS 韧性。Phase 3 实现削减/大僵直/处决；本册只让 UI 能读到它。
	UPROPERTY(BlueprintReadOnly, Category="Poise")
	FGameplayAttributeData Poise;
	ATTRIBUTE_ACCESSORS(UFirstAttributeSet, Poise)

	UPROPERTY(BlueprintReadOnly, Category="Poise")
	FGameplayAttributeData MaxPoise;
	ATTRIBUTE_ACCESSORS(UFirstAttributeSet, MaxPoise)
```

### 6.2 源文件：初始化 + 夹紧 + 广播

> 【改动位置】文件 `FirstAttributeSet.cpp`
> 【修改内容】构造函数初始化 Poise；`PostGameplayEffectExecute` 末尾广播三个百分比

构造函数新增：

```cpp
	InitPoise(1.f);
	InitMaxPoise(1.f);
```

新增 include：

```cpp
#include "Components/UI/PawnUIComponent.h"
#include "Interfaces/PawnUIInterface.h"
```

`PostGameplayEffectExecute` 里新增韧性夹紧（放在 Stamina 夹紧后面）：

```cpp
	if (Data.EvaluatedData.Attribute == GetPoiseAttribute())
	{
		SetPoise(FMath::Clamp(GetPoise(), 0.f, GetMaxPoise()));
	}
```

新增广播的**具体位置**：打开 `FirstAttributeSet.cpp`，翻到 `PostGameplayEffectExecute` 函数的结尾。
找到 `if (Data.EvaluatedData.Attribute == GetDamageTakenAttribute())` 这个 if 块的右花括号 `}`，
把广播代码插在**这个右花括号之后、函数最后的右花括号 `}` 之前**。当前结尾长这样：

```cpp
		}
		
	}            ← if (GetDamageTakenAttribute()) 的右花括号，广播从这里之后插入

}                ← PostGameplayEffectExecute 的右花括号（函数结束，保持不动）
```

插入后，结尾变成：

```cpp
		}
		
	}

	// —— UI 广播：只在影响这几个属性的 Effect 执行后更新一次 ——
	...（下面的代码块）

}
```

下面是**要插入的完整代码块**：

```cpp
	// —— UI 广播：只在影响这几个属性的 Effect 执行后更新一次 ——
	const FGameplayAttribute& EvaluatedAttribute = Data.EvaluatedData.Attribute;
	if (EvaluatedAttribute == GetHealthAttribute() ||
		EvaluatedAttribute == GetMaxHealthAttribute() ||
		EvaluatedAttribute == GetStaminaAttribute() ||
		EvaluatedAttribute == GetMaxStaminaAttribute() ||
		EvaluatedAttribute == GetPoiseAttribute() ||
		EvaluatedAttribute == GetMaxPoiseAttribute() ||
		EvaluatedAttribute == GetDamageTakenAttribute())
	{
		// 缓存 UI 接口，避免每次执行都 Cast。
		if (!CachedPawnUIInterface.IsValid())
		{
			CachedPawnUIInterface = Cast<IPawnUIInterface>(GetOwningActor());
		}

		if (UPawnUIComponent* UIComponent = CachedPawnUIInterface.IsValid()
			? CachedPawnUIInterface->GetPawnUIComponent()
			: nullptr)
		{
			const float HealthPercent = GetMaxHealth() > 0.f ? GetHealth() / GetMaxHealth() : 0.f;
			const float StaminaPercent = GetMaxStamina() > 0.f ? GetStamina() / GetMaxStamina() : 0.f;
			const float PoisePercent = GetMaxPoise() > 0.f ? GetPoise() / GetMaxPoise() : 0.f;

			UIComponent->OnCurrentHealthChanged.Broadcast(HealthPercent);
			UIComponent->OnCurrentStaminaChanged.Broadcast(StaminaPercent);
			UIComponent->OnCurrentPoiseChanged.Broadcast(PoisePercent);
		}
	}
```

`FirstAttributeSet.h` 的 `private:` 区域新增缓存：

```cpp
	// 缓存的 UI 接口，避免每帧/每次执行都 Cast。
	TWeakInterfacePtr<IPawnUIInterface> CachedPawnUIInterface;
```

`FirstAttributeSet.h` 的类声明上方还需要前置声明：

```cpp
class IPawnUIInterface;
```

编译闸门 4。

---

## 7. 第五步：C++ 控件基类（绑定委托 + 更新进度条）

### 7.1 UFirstPlayerHUDWidget

用编辑器向导创建：

1. **工具（Tools）→ 新建 C++ 类（New C++ Class）** → 勾选 **显示所有类（Show All Classes）**；
2. 搜索并选择父类 **`UserWidget`**（即 `UUserWidget`）；
3. 类名填写 `FirstPlayerHUDWidget`（不要带 U 前缀，向导会自动加 U，最终类名 `UFirstPlayerHUDWidget`）；
4. 创建后整体替换为下面的内容（新建文件）。

> 【改动位置】新建文件 `FirstPlayerHUDWidget.h` / `FirstPlayerHUDWidget.cpp`
> 【新增内容】自动找到玩家 → 绑定血量/精力委托 → 更新两个进度条；蓝图中只需摆放同名控件

`Source/First/Public/UI/FirstPlayerHUDWidget.h`：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FirstPlayerHUDWidget.generated.h"

class ADKCharacter;
class UProgressBar;

/**
 * 玩家左下角 HUD。
 * 蓝图里必须放置两个进度条，名字要和下面完全一致：
 *   HealthBar、StaminaBar
 */
UCLASS()
class FIRST_API UFirstPlayerHUDWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;

	// 找到玩家并绑定委托、读取当前值。
	void BindToPlayer(ADKCharacter* InPlayer);

	UFUNCTION()
	void HandleHealthChanged(float NewPercent);

	UFUNCTION()
	void HandleStaminaChanged(float NewPercent);

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UProgressBar> HealthBar;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UProgressBar> StaminaBar;

	// 出生时 HUD 创建可能早于玩家被 Possess，取不到玩家就下一帧重试。
	void RetryBindPlayer();
};
```

`Source/First/Private/UI/FirstPlayerHUDWidget.cpp`：

```cpp
#include "UI/FirstPlayerHUDWidget.h"

#include "AbilitySystem/FirstAttributeSet.h"
#include "Character/DKCharacter.h"
#include "Components/ProgressBar.h"
#include "Components/UI/DKUIComponent.h"
#include "TimerManager.h"

void UFirstPlayerHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 出生时序：角色 BeginPlay 创建 HUD 时，玩家控制器可能还没 Possess 这个角色，
	// GetOwningPlayerPawn() 会返回空。先取一次，取不到就下一帧重试。
	ADKCharacter* Player = Cast<ADKCharacter>(GetOwningPlayerPawn());
	if (Player)
	{
		BindToPlayer(Player);
	}
	else
	{
		RetryBindPlayer();
	}
}

void UFirstPlayerHUDWidget::RetryBindPlayer()
{
	// HUD 已被移除/销毁时不再重试。
	if (!IsInViewport())
	{
		return;
	}

	ADKCharacter* Player = Cast<ADKCharacter>(GetOwningPlayerPawn());
	if (Player)
	{
		BindToPlayer(Player);
	}
	else
	{
		GetWorld()->GetTimerManager().SetTimerForNextTick(this, &ThisClass::RetryBindPlayer);
	}
}

void UFirstPlayerHUDWidget::BindToPlayer(ADKCharacter* InPlayer)
{
	if (!InPlayer)
	{
		return;
	}

	if (UDKUIComponent* DKUI = InPlayer->GetDKUIComponent())
	{
		DKUI->OnCurrentHealthChanged.AddDynamic(this, &ThisClass::HandleHealthChanged);
		DKUI->OnCurrentStaminaChanged.AddDynamic(this, &ThisClass::HandleStaminaChanged);

		// 出生时 GE 已经广播过一次（当时 HUD 还没创建），这里补读当前值。
		const UFirstAttributeSet* AttrSet = InPlayer->GetFirstAttributeSet();
		if (AttrSet)
		{
			HandleHealthChanged(AttrSet->GetMaxHealth() > 0.f ? AttrSet->GetHealth() / AttrSet->GetMaxHealth() : 0.f);
			HandleStaminaChanged(AttrSet->GetMaxStamina() > 0.f ? AttrSet->GetStamina() / AttrSet->GetMaxStamina() : 0.f);
		}
	}
}

void UFirstPlayerHUDWidget::HandleHealthChanged(float NewPercent)
{
	if (HealthBar)
	{
		HealthBar->SetPercent(NewPercent);
	}
}

void UFirstPlayerHUDWidget::HandleStaminaChanged(float NewPercent)
{
	if (StaminaBar)
	{
		StaminaBar->SetPercent(NewPercent);
	}
}
```

### 7.2 UFirstBossHUDWidget

用编辑器向导创建：

1. **工具（Tools）→ 新建 C++ 类（New C++ Class）** → 勾选 **显示所有类（Show All Classes）**；
2. 搜索并选择父类 **`UserWidget`**（即 `UUserWidget`）；
3. 类名填写 `FirstBossHUDWidget`（不要带 U 前缀，向导会自动加 U，最终类名 `UFirstBossHUDWidget`）；
4. 创建后整体替换为下面的内容（新建文件）。

> 【改动位置】新建文件 `FirstBossHUDWidget.h` / `FirstBossHUDWidget.cpp`
> 【新增内容】自动找到场景里的 BOSS → 设置名字 → 绑定血量/韧性委托

`Source/First/Public/UI/FirstBossHUDWidget.h`：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FirstBossHUDWidget.generated.h"

class ABossCharacter;
class UProgressBar;
class UTextBlock;

/**
 * BOSS 顶部大血条。
 * 蓝图里必须放置三个控件，名字要和下面完全一致：
 *   BossNameText、BossHealthBar、BossPoiseBar
 */
UCLASS()
class FIRST_API UFirstBossHUDWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;

	// 找到 BOSS 并绑定委托、读取当前值。
	void BindToBoss(ABossCharacter* InBoss);

	UFUNCTION()
	void HandleHealthChanged(float NewPercent);

	UFUNCTION()
	void HandlePoiseChanged(float NewPercent);

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UTextBlock> BossNameText;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UProgressBar> BossHealthBar;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UProgressBar> BossPoiseBar;
};
```

`Source/First/Private/UI/FirstBossHUDWidget.cpp`：

```cpp
#include "UI/FirstBossHUDWidget.h"

#include "AbilitySystem/FirstAttributeSet.h"
#include "Character/BossCharacter.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/UI/EnemyUIComponent.h"
#include "EngineUtils.h"

void UFirstBossHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 本册单 BOSS 场景：直接找场景里第一个 BOSS。
	// 以后多 BOSS / 动态生成时，改为由玩家 HUD 传入当前锁定目标。
	for (TActorIterator<ABossCharacter> It(GetWorld()); It; ++It)
	{
		BindToBoss(*It);
		break;
	}
}

void UFirstBossHUDWidget::BindToBoss(ABossCharacter* InBoss)
{
	if (!InBoss)
	{
		return;
	}

	if (BossNameText)
	{
		BossNameText->SetText(InBoss->BossName);
	}

	if (UEnemyUIComponent* EnemyUI = InBoss->GetEnemyUIComponent())
	{
		EnemyUI->OnCurrentHealthChanged.AddDynamic(this, &ThisClass::HandleHealthChanged);
		EnemyUI->OnCurrentPoiseChanged.AddDynamic(this, &ThisClass::HandlePoiseChanged);

		// 补读当前值（出生广播发生在 HUD 创建之前）。
		const UFirstAttributeSet* AttrSet = InBoss->GetFirstAttributeSet();
		if (AttrSet)
		{
			HandleHealthChanged(AttrSet->GetMaxHealth() > 0.f ? AttrSet->GetHealth() / AttrSet->GetMaxHealth() : 0.f);
			HandlePoiseChanged(AttrSet->GetMaxPoise() > 0.f ? AttrSet->GetPoise() / AttrSet->GetMaxPoise() : 0.f);
		}
	}
}

void UFirstBossHUDWidget::HandleHealthChanged(float NewPercent)
{
	if (BossHealthBar)
	{
		BossHealthBar->SetPercent(NewPercent);
	}
}

void UFirstBossHUDWidget::HandlePoiseChanged(float NewPercent)
{
	if (BossPoiseBar)
	{
		BossPoiseBar->SetPercent(NewPercent);
	}
}
```

编译闸门 5：控件基类编译通过。

---

## 8. 第六步：编辑器资产配置

### 8.1 创建 WBP_PlayerHUD（左下角）

**先理解控件层级**（回答"安全区是什么 / 为什么没有画布"）：

```text
画布（Canvas Panel，根节点，负责定位）——新建控件蓝图时自动生成
└─ 安全区（Safe Area，负责避让屏幕边缘遮挡区）
   └─ 垂直框（Vertical Box，负责上下排列）
      ├─ HealthBar（血条）
      └─ StaminaBar（精力条）
```

- **画布（Canvas Panel）**：控件蓝图的根节点，所有控件靠它的"锚点（Anchors）+ 位置"定位。它一直在，不需要新建；
- **安全区（Safe Area）**：自动避开手机刘海、圆角、系统任务栏、电视过扫等屏幕边缘遮挡区的容器。在电脑上通常看不出效果，包一层是习惯，以后上手机/主机不会被裁切；
- **垂直框（Vertical Box）**：**可选**，不是必须的。作用是让血条、精力条自动上下排列；如果你觉得不好调整，完全可以直接把两条进度条放进画布/安全区，各自设锚点和偏移。
  - 用垂直框的诀窍：垂直框本身**没有"间距"属性**，两条进度条之间的距离要在**每个子控件的"槽位（Slot）→ 填充（Padding）"**里设置（例如 HealthBar 的槽位 Padding 下边距填 8）；对齐则在子控件槽位的"水平对齐（Horizontal Alignment）"里调。
  - 不用垂直框的替代法：两条进度条都锚点左下角（0,0）、对齐 (0,0)，`HealthBar` 位置 (0,0)，`StaminaBar` 位置 (0,32)（血条高 24 + 间距 8），手动控制最直观。

> ⚠️ 检查：层级（Hierarchy）面板里**必须能看到"画布"这一层**。如果只有"进度条"直接挂在根节点下面，
> 说明默认画布被删了、进度条被当成了根控件——先删掉裸进度条，在设计器空白处右键 → **添加控件 → 画布（Canvas Panel）**，
> 让它成为根，再按下面的步骤重新搭。

操作步骤：

1. 内容浏览器新建 `/Game/UI/` 文件夹；
2. 右键 → **用户界面（User Interface）→ 控件蓝图（Widget Blueprint）**，命名 `WBP_PlayerHUD`；
3. 打开后，点工具栏 **类默认值（Class Defaults）** → **父类（Parent Class）**改为 `FirstPlayerHUDWidget`（C++ 类，搜索时勾选"显示所有类"）；
4. 回到设计器，根节点是画布（Canvas Panel），在画布里放一个**安全区（Safe Area）**，然后**二选一**：
   - **方案 A（推荐，不用垂直框）**：安全区里直接放两个**进度条（Progress Bar）**，`HealthBar` 位置 (0,0)、`StaminaBar` 位置 (0,32)；
   - **方案 B（用垂直框自动排列）**：安全区里放一个垂直框，垂直框里放两个进度条，间距在子控件槽位的 Padding 里设 8；
   - 无论哪个方案：命名必须与 C++ 完全一致——`HealthBar`（红色、宽 500 高 24）、`StaminaBar`（青色、宽 500 高 16）；
   - 进度条**必须给填充图指定一张纯白贴图**（引擎自带：`/Engine/EngineResources/WhiteTexture`，或 `WhiteSquareTexture`）：选中进度条 → 细节面板 → **样式（Style）→ 填充图片（Fill Image）→ 图像（Image）** 选白色贴图；填充图的 **着色（Tint）保持白色**；
   - 颜色靠进度条自己的 **"填充颜色和不透明度（Fill Color and Opacity）"** 调：`HealthBar` 红色、`StaminaBar` 青色；背景图建议也选一张贴图（白色或深色描边，看喜好）；
   - ⚠️ **不要留 `Image = None`**：没有贴图的盒体画笔在**编辑器里预览是白色占位，运行时渲染不可靠（会变成黑条/空条）**。这是"蓝图里设了红色、游戏里却是黑的"最常见原因
   - （可选）Warrior 迁移来的素材在 `/Game/Assets/Textures/UI/` 下：进度条填充图可直接选材质 `MI_HeroHealthBar`（或主材质 `M_ProgreeBarMaster`），背景图可用 `Gui_parts/big_background` 或 `Gui_parts/Frame_big`，更精致
5. 选中**安全区**：锚点左下角（0,0），对齐 (0,0)，偏移 (40, 40)（方案 A 不需要再管垂直框）；
6. 编译并保存。

> 不需要写任何蓝图节点：绑定和更新都由 C++ 基类完成，蓝图只负责布局。

### 8.2 创建 WBP_BossHUD（顶部大血条）

层级和 8.1 同理，根节点是画布（BOSS 血条不需要安全区，除非以后要适配手机）。**垂直框可用可不用**：

```text
画布（根节点）
└─ 三个控件都锚点顶部居中（0.5, 0）、对齐 X=0.5，用 Y 偏移上下排开：
   BossNameText（偏移 0）→ BossHealthBar（偏移 56）→ BossPoiseBar（偏移 100）
（或者用垂直框包住三个控件，锚点顶部居中，由它自动排列）
```

1. 右键 → **用户界面 → 控件蓝图**，命名 `WBP_BossHUD`；
2. **类默认值 → 父类**改为 `FirstBossHUDWidget`；
3. 设计器里放三个控件（用垂直框就放进垂直框；不用就各自设锚点/偏移），命名必须一致：
   - `BossNameText`（**文本（Text Block）**，字号 40，居中，白色/金色）
   - `BossHealthBar`（**进度条**，红色，宽 1200 高 36）
   - `BossPoiseBar`（**进度条**，银白色，宽 1200 高 10）
   - 两个进度条同样：**样式 → 填充图片 → 图像** 选引擎自带白色贴图（`/Engine/EngineResources/WhiteTexture`），着色（Tint）保持白色；颜色用进度条自己的 **"填充颜色和不透明度（Fill Color and Opacity）"** 调——`BossHealthBar` 红色、`BossPoiseBar` 银白色；**不要留 Image = None**（运行时会渲染成黑条）
   - （可选）BOSS 血条填充图可改用迁移来的 `MI_BossHealthBar`，背景用 `Gui_parts/big_background`；名字字体用 `Cinzel`（`/Game/Assets/Textures/UI/Fonts/Cinzel/`）
4. 锚点：三个控件都**顶部居中（0.5, 0）**、对齐 X=0.5，按上面图示用 Y 偏移排开（或用垂直框统一控制）；
5. 名字和血条、血条和韧性条之间各留 8px（垂直框方案在子控件槽位 Padding 里设）；
6. 编译并保存。

### 8.3 给 GE_Boss_Initialize 加韧性修饰符

打开 `GE_Boss_Initialize` → **修饰符（Modifiers）** → 添加两项：

| 顺序 | 属性 | 修饰符操作 | 数值 |
|---|---|---|---|
| 倒数第 2 | `MaxPoise` | 重载（Override） | 100 |
| 倒数第 1 | `Poise` | 重载（Override） | 100 |

保存。

### 8.4 配置 BP_DKCharacter 与 BP_Boss

`BP_DKCharacter` → 类默认值：

| 属性 | 值 |
|---|---|
| UI \| HUD → Player HUD Widget Class | `WBP_PlayerHUD` |
| UI \| HUD → Boss HUD Widget Class | `WBP_BossHUD` |

`BP_Boss` → 类默认值：

| 属性 | 值 |
|---|---|
| Boss \| UI → Boss Name | 输入你的 BOSS 名字（例如"古龙骑士"） |

编译并保存两个蓝图。

---

## 9. 第七步：编译顺序汇总

```text
闸门 1：Build.cs 加 UMG
闸门 2：UI 组件与接口（PawnUIComponent / DKUIComponent / EnemyUIComponent / PawnUIInterface）
闸门 3：角色接入（BaseCharacter / DKCharacter / BossCharacter）
闸门 4：AttributeSet（Poise 属性 + 广播）
闸门 5：控件基类（FirstPlayerHUDWidget / FirstBossHUDWidget）
最后：编辑器资产（WBP_PlayerHUD / WBP_BossHUD / GE / 蓝图配置）
```

每完成一个闸门做一次普通 Build。

> ✅ 顺序原则（必守）：**任何类在第一次被引用前，都必须已经创建并通过编译**。
> 本册能按"组件接口 → 角色 → 属性 → 控件基类"的顺序走通，关键在第 5.2 步：
> 角色只持有 `TSubclassOf<UUserWidget>`（引擎自带类），不直接引用后面才创建的
> `UFirstPlayerHUDWidget` / `UFirstBossHUDWidget`；两个控件类只依赖角色（第 5 步）和
> Poise 属性（第 6 步），所以放到最后创建，依赖全部前置。

---

## 10. 测试清单

| 测试 | 操作 | 预期结果 |
|---|---|---|
| 玩家血条初始 | 运行 PIE | 左下角血条为满（Health 100/100） |
| 玩家精力条初始 | 运行 PIE | 精力条为满（100/100） |
| 玩家掉血 | 被敌人/测试伤害打中 | 血条按百分比下降 |
| 玩家精力消耗/恢复 | 闪避消耗精力、等待恢复 | 精力条下降 → 自动回满 |
| BOSS 血条显示 | 进入战斗后 | 顶部出现大血条，名字正确 |
| BOSS 掉血 | 攻击 BOSS | 顶部血条下降 |
| 韧性条 | 观察 BOSS | 韧性条满值 100%（Phase 3 削减后才会变化） |
| 布局 | 不同分辨率窗口 | 左下角、顶部位置不越界 |
| 重进 | 重启编辑器再运行 | 控件引用不丢失，委托正常 |

---

## 11. 常见问题与排查

| 现象 | 原因 | 修复 |
|---|---|---|
| 编译报 C2065："DKUIComponent" 未声明的标识符（在 DKCharacter.h 的访问器里） | 头文件只加了前置声明和访问函数，**漏了成员变量本身**；前置声明只解决类型名，不解决 `return DKUIComponent;` 里的变量 | 按 5.2 在 `DKCharacter.h` 的 `private:` 区域补 `UPROPERTY(...) TObjectPtr<UDKUIComponent> DKUIComponent;`，并在 cpp 构造函数里 `CreateDefaultSubobject`；同时检查 HUD 两个类成员和 `BeginPlay()` 声明是否也补齐 |
| 编译报 C2338：static_assert "The given OwningObject is not of a supported type for use with CreateWidget" | `CreateWidget<UUserWidget>(this, ...)` 把角色当成了拥有者，而 `CreateWidget` 只支持 `UWorld` / 控件 / 玩家控制器 | 把 `this` 改成 `GetWorld()`（或先取 `GetWorld()->GetFirstPlayerController()` 再传） |
| 运行时血条是黑色/看不到红色填充 | 进度条的**填充图片（Fill Image）的 Image = None**：编辑器里预览是白色占位，运行时没有贴图的盒体画笔渲染成黑条；或进度条自己的"填充颜色和不透明度"是黑色/透明度 0，或控件"颜色和不透明度"是黑色 | 给填充图片选引擎自带白色贴图（`/Engine/EngineResources/WhiteTexture`），Tint 保持白色，用进度条自己的"填充颜色和不透明度"调成红色/青色；再检查两个颜色属性不是黑/透明。设计器里 Percent=0 时看不到填充是正常的，可临时把 Percent 设 1 预览 |
| 玩家血条一直不动（空条/0%） | HUD 在角色 `BeginPlay` 里创建时，玩家控制器还没 Possess 该角色，`GetOwningPlayerPawn()` 返回空，绑定没执行 | 用 7.1 的 `RetryBindPlayer()` 下一帧重试，或把 HUD 创建延迟到 Possess 之后 |
| HUD 没显示 | BP_DKCharacter 的 HUD 类没填，或控件父类没改 | 检查 8.1 / 8.2 / 8.4 |
| 进度条不动 | 控件名字和 C++ 不一致（BindWidget 绑定失败） | 确认名字精确为 `HealthBar` / `StaminaBar` / `BossHealthBar` / `BossPoiseBar` |
| 编译报"找不到 UMG" | Build.cs 没加模块 | 检查第 3 步 |
| 属性没广播 | AttributeSet 的缓存接口为空 | 确认角色实现了 `IPawnUIInterface` 且 `GetPawnUIComponent()` 返回组件 |
| BOSS 名字空白 | BP_Boss 的 BossName 没填 | 在类默认值里填名字 |
| 只有玩家条、没有 BOSS 条 | 场景里没有 BOSS，或 BOSS 生成晚于 HUD | 先把 BP_Boss 放进地图；动态生成场景以后再改为传入引用 |
| 控件里找不到父类 First...Widget | 搜索没显示 C++ 类 | 父类窗口勾选"显示所有类"再搜 |
| 层级里只有"进度条"，没有画布/安全区/垂直框 | 默认画布被删了，进度条被当成了根控件 | 删掉裸进度条，在设计器空白处右键 → 添加控件 → 画布（Canvas Panel）作为根，再按 8.1 层级重新搭；控件名必须精确为 `HealthBar` / `StaminaBar` |

---

## 12. 后续扩展

1. **进入战斗淡入 / 脱战淡出**：给 `EnemyUIComponent` 加一个"进入战斗"委托（BOSS 索敌成功时触发），控制 `WBP_BossHUD` 的透明度动画；
2. **受击白条残留**：血条双层（白条延迟缩减），魂系受击反馈；
3. **大僵直 / 可处决特效**：按决策书 A3/A5，韧性 ≤50% 时韧性条闪烁，归零时可处决时血条发光；
4. **头顶小血条**：`WidgetComponent` 挂在 BOSS 头顶（可选，和顶部大血条二选一或并存）；
5. **玩家连击计数 / 状态提示**：在 `DKUIComponent` 上扩展。
