# BOSS 基本巡逻实现指导

> 适用项目：`First`（UE 5.6 / C++ / GAS / 内置 AI）
> 引擎版本：Unreal Engine 5.6 **中文版**；文中编辑器操作按中文版界面名称标注，括号内为英文名称。
> 本文只提供实现指导，不会自动修改你的 C++、蓝图或动画资源。
> 配套阅读：先读《BOSS设计决策书》第 3.1 节与第 7 节 Phase 1；本册完成后，继续读《BOSS 索敌与警戒实现指导》。
> ⚠️ **初学者必读**：本文里的代码已经是 UE 5.6 修正后的正确版本。还没创建文件的同学直接照抄；已经按旧版本抄过代码的同学，请先翻到文末 **第 14 节：UE 5.6 修正对照**，按表改好自己的文件再编译。

---

## 0. 先看结论（本册完成后得到什么）

把 BOSS 放进测试地图后，它会：

```text
出生时记录当前位置为“家点”
→ 在家点周围 PatrolRadius 半径内随机选一个可达点
→ 移动到该点
→ 到达后停留 PatrolWaitTime 秒
→ 重复“找点 → 移动 → 等待”
```

BOSS 在玩家进入警戒范围之前（警戒是下一册的内容），始终在家点周围的巡逻范围内循环移动，不会跑出巡逻范围。

---

## 1. 本次要解决的问题

BOSS 战设计里，BOSS 的感知分为三个同心范围：

```text
巡逻范围（最大）  玩家不在范围内 → 巡逻
警戒范围（居中）  玩家进入 → 警戒、转头、对峙、之后追击（下一册）
攻击范围（最小）  玩家进入 → 攻击 + 对峙循环（后续手册）
```

本册只完成最外层：**巡逻范围**。做的是“BOSS 在没有目标时，如何在自己地盘里持续移动”，并为下一册的索敌提供两个基础件：`ABossCharacter`（放巡逻参数）和 `ABossAIController`（启动行为树）。

---

## 2. 设计依据（对应 BOSS 设计决策书）

| 决策 | 结论 | 本册做法 |
|---|---|---|
| A2 | 使用内置 AIPerception + BehaviorTree + Blackboard | 巡逻分支用 BehaviorTree + Blackboard 驱动 |
| D1 | 巡逻点推荐“场景摆放 Actor 数组” | 本方案改用“家点 + 随机可达点”，更贴合你说的“在巡逻范围内巡逻”，不用在场景摆一堆点；Actor 数组作为后续扩展保留 |
| D2 | 巡逻速度默认奔跑、和追击一致 | 直接用角色 `MaxWalkSpeed`（默认 500），暂不做巡逻/追击双速度 |
| D26 | 核心用 C++，简单判断用蓝图/内置节点 | 找巡逻点是 C++ 任务；移动、等待用引擎内置节点 |

---

## 3. 涉及文件与资源

### 3.1 新增 C++

| 文件（建议路径） | 父类 | 职责 |
|---|---|---|
| `Source/First/Public/Character/BossCharacter.h`<br>`Source/First/Private/Character/BossCharacter.cpp` | `AEnemyCharacter` | 巡逻参数（家点、巡逻半径）、移动配置 |
| `Source/First/Public/Controllers/BossAIController.h`<br>`Source/First/Private/Controllers/BossAIController.cpp` | `AAIController` | 初始化黑板初值并启动行为树 |
| `Source/First/Public/AI/Tasks/BTTask_FindPatrolLocation.h`<br>`Source/First/Private/AI/Tasks/BTTask_FindPatrolLocation.cpp` | `UBTTaskNode` | 在巡逻半径内找随机可达点并写入黑板 |

### 3.2 新增/修改资产

| 资产 | 操作 |
|---|---|
| `BB_Boss` | 新建黑板（HomeLocation、PatrolLocation） |
| `BT_Boss` | 新建行为树（巡逻分支） |
| `BP_BossAIController` | 新建蓝图，父类 `ABossAIController`，在这里把 `BT_Boss` 配给 BossBehaviorTree |
| `BP_Boss` | 新建蓝图，父类 `ABossCharacter`，AIController Class 填 `BP_BossAIController` |
| `GE_Boss_Initialize`、`DA_BossStartUpData` | 新建（参考已有的 `GE_Enemy_Initialize` 做法），让 BOSS 有属性 |
| `NavMeshBoundsVolume` | 测试地图中必须有，覆盖巡逻范围 |

### 3.3 本册改动总览（按文件找位置）

| 文件 | 位置 | 操作 | 新增内容 |
|---|---|---|---|
| `BossCharacter.h` | 类的 `public:` 区域 | 新增 | 巡逻参数：HomeLocation、bUseSpawnLocationAsHome、PatrolRadius；BeginPlay 声明 |
| `BossCharacter.cpp` | 构造函数 / BeginPlay | 新增 | MaxWalkSpeed、bOrientRotationToMovement 设置；出生位置记录为家点 |
| `BossAIController.h` | public / protected 区域 | 新增 | BossBehaviorTree、GetBossCharacter、OnPossess、InitializeBossBlackboard 声明 |
| `BossAIController.cpp` | 构造函数 / OnPossess / InitializeBossBlackboard | 新增 | 行为树专用组件、黑板初值、RunBehaviorTree |
| `BTTask_FindPatrolLocation.h` | 类声明 | 新增 | HomeLocationKey / PatrolLocationKey、ExecuteTask 声明 |
| `BTTask_FindPatrolLocation.cpp` | ExecuteTask | 新增 | 随机可达点查找与写黑板 |

> 说明：本册 6 个文件都是**新建文件**，不存在旧代码；下面每步的"整体替换"意思是"把向导生成的占位内容换成完整内容"，全部都是新增。

---

## 4. 第一步：模块依赖（必须先做）

文件：`Source/First/First.Build.cs`

在 `PublicDependencyModuleNames` 中新增两个模块：

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
			"NavigationSystem" });
```

讲解：

- `AIModule`：提供 `AAIController`、`BehaviorTree`、`Blackboard`、`AIPerception` 全部 AI 基础类型；
- `NavigationSystem`：提供“在半径内找随机可达点”的查询（`GetRandomReachablePointInRadius`）；
- `GameplayAbilities` / `GameplayTags` 已经存在，不动。

改完先做一次普通 Build，确认模块可解析。

---

## 5. 第二步：创建 ABossCharacter

### 5.1 用编辑器向导创建类

1. 顶部菜单选择 **工具（Tools）→ 新建 C++ 类（New C++ Class）**；
2. 勾选右下角 **显示所有类（Show All Classes）**；
3. 搜索父类 `EnemyCharacter`（即 `AEnemyCharacter`）；
4. 类名填写 `BossCharacter`；
5. 确认路径位于 `Source/First` 下，点击 **创建类**。

向导生成两个文件后，把下面的完整内容整体替换进去（**两个文件都是新建，内容全部为新增**，改动位置见 5.2 / 5.3 开头）。

### 5.2 头文件

> 【改动位置】文件 `BossCharacter.h`，类的 `public:` 区域
> 【新增内容】巡逻参数属性：`HomeLocation`、`bUseSpawnLocationAsHome`、`PatrolRadius`；`BeginPlay()` 声明

`Source/First/Public/Character/BossCharacter.h`：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Character/EnemyCharacter.h"
#include "BossCharacter.generated.h"

/**
 * BOSS 角色。
 *
 * 本册职责：提供巡逻参数（家点、巡逻半径），并完成移动相关的基础配置。
 * 索敌（警戒范围/攻击范围）、攻击、韧性、处决等内容在后续手册中继续扩展。
 */
UCLASS()
class FIRST_API ABossCharacter : public AEnemyCharacter
{
	GENERATED_BODY()

public:
	ABossCharacter();

	// —— 巡逻参数 ——

	// 巡逻圆心。默认用出生位置作为家点。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Patrol", meta=(AllowPrivateAccess="true"))
	FVector HomeLocation = FVector::ZeroVector;

	// true：出生时把当前出生位置写入 HomeLocation；
	// false：使用上面手动填写的 HomeLocation（例如做固定战场的 BOSS）。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Patrol", meta=(AllowPrivateAccess="true"))
	bool bUseSpawnLocationAsHome = true;

	// 巡逻半径：BOSS 只在 HomeLocation 周围这个半径内选取巡逻点。
	// 决策书建议值 800～1200，默认 1200。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|Patrol", meta=(AllowPrivateAccess="true"))
	float PatrolRadius = 1200.f;

protected:
	virtual void BeginPlay() override;
};
```

### 5.3 源文件

> 【改动位置】文件 `BossCharacter.cpp`，构造函数 + `BeginPlay()`
> 【新增内容】构造函数：`MaxWalkSpeed` 与 `bOrientRotationToMovement` 设置；`BeginPlay()`：把出生位置写入 `HomeLocation`

`Source/First/Private/Character/BossCharacter.cpp`：

```cpp
#include "Character/BossCharacter.h"

#include "GameFramework/CharacterMovementComponent.h"

ABossCharacter::ABossCharacter()
{
	// 巡逻与追击共用同一个速度（决策书 D2：默认奔跑）。
	// 以后想区分“巡逻慢走/追击奔跑”，再通过黑板键动态改 MaxWalkSpeed。
	GetCharacterMovement()->MaxWalkSpeed = 500.f;

	// 移动时自动转向移动方向：MoveTo 驱动巡逻时角色会自然面向前进方向。
	GetCharacterMovement()->bOrientRotationToMovement = true;
}

void ABossCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (bUseSpawnLocationAsHome)
	{
		HomeLocation = GetActorLocation();
	}
}
```

讲解：

- `HomeLocation` 是巡逻的圆心，不是每个巡逻点；所有随机点都从它周围取；
- `bUseSpawnLocationAsHome` 让 BOSS 摆在哪就巡逻在哪，不用手动填坐标；
- `bOrientRotationToMovement = true` 必须开着，否则 `MoveTo` 移动时角色不会转向；
- 巡逻参数都是 `EditDefaultsOnly`，最终数值在 `BP_Boss` 里调，不写死在 C++。

---

## 6. 第三步：创建 ABossAIController

### 6.1 用编辑器向导创建类

1. **工具（Tools）→ 新建 C++ 类（New C++ Class）** → 勾选 **显示所有类（Show All Classes）**；
2. 搜索父类 `AIController`（即 `AAIController`）；
3. 类名填写 `BossAIController`；
4. 确认路径位于 `Source/First` 下，创建后把下面的完整内容整体替换进去（**新建文件，内容全部为新增**，改动位置见 6.2 / 6.3 开头）。

### 6.2 头文件

> 【改动位置】文件 `BossAIController.h`，`public:` 与 `protected:` 区域
> 【新增内容】`BossBehaviorTree` 属性、`GetBossCharacter()` 声明；`OnPossess()`、`InitializeBossBlackboard()` 声明

`Source/First/Public/Controllers/BossAIController.h`：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "BossAIController.generated.h"

class ABossCharacter;
class UBehaviorTree;

/**
 * BOSS 的 AI 控制器。
 *
 * 本册职责：Possess 后把巡逻参数写入黑板，并启动巡逻行为树。
 * 视觉感知、警戒、追击在下一册《BOSS 索敌与警戒实现指导》中扩展。
 */
UCLASS()
class FIRST_API ABossAIController : public AAIController
{
	GENERATED_BODY()

public:
	ABossAIController();

	// 在 BP_BossAIController 的类默认值中配置：巡逻行为树资产。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Boss|AI")
	TObjectPtr<UBehaviorTree> BossBehaviorTree;

	// 快捷访问当前控制的 BOSS。
	UFUNCTION(BlueprintCallable, Category="Boss|AI")
	ABossCharacter* GetBossCharacter() const;

protected:
	virtual void OnPossess(APawn* InPawn) override;

	// 初始化黑板键初值，并启动行为树。
	void InitializeBossBlackboard();
};
```

### 6.3 源文件

> 【改动位置】文件 `BossAIController.cpp`，构造函数 + `GetBossCharacter` + `OnPossess` + `InitializeBossBlackboard`
> 【新增内容】构造函数：Blackboard/BrainComponent 换成行为树专用组件；`OnPossess`：调用初始化；`InitializeBossBlackboard`：黑键初值 + `RunBehaviorTree`

`Source/First/Private/Controllers/BossAIController.cpp`：

```cpp
#include "Controllers/BossAIController.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "Character/BossCharacter.h"

ABossAIController::ABossAIController()
{
	// AIController 默认会创建黑键组件和脑组件；
	// 这里替换成行为树专用的两个组件，是标准的 BehaviorTree 控制器写法。
	Blackboard = CreateDefaultSubobject<UBlackboardComponent>(TEXT("BossBlackboard"));
	BrainComponent = CreateDefaultSubobject<UBehaviorTreeComponent>(TEXT("BossBrain"));
}

ABossCharacter* ABossAIController::GetBossCharacter() const
{
	return Cast<ABossCharacter>(GetPawn());
}

void ABossAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	InitializeBossBlackboard();
}

void ABossAIController::InitializeBossBlackboard()
{
	ABossCharacter* Boss = GetBossCharacter();
	UBlackboardComponent* BB = GetBlackboardComponent();

	if (!Boss)
	{
		UE_LOG(LogTemp, Error, TEXT("[BossAI] 缺少 BOSS 角色（请确认 AI 控制器类挂在 BP_Boss 上）"));
		return;
	}
	if (!BB)
	{
		UE_LOG(LogTemp, Error, TEXT("[BossAI] 缺少黑键组件"));
		return;
	}
	if (!BossBehaviorTree)
	{
		UE_LOG(LogTemp, Error, TEXT("[BossAI] 缺少行为树资产（请在 BP_BossAIController 类默认值中把 BossBehaviorTree 设为 BT_Boss）"));
		return;
	}

	// 先让黑键组件使用行为树配套的黑板资产，之后才能写入键值。
	// 顺序不能反：先 UseBlackboard，再 SetValueAsVector。
	if (UBlackboardData* BBAsset = BossBehaviorTree->GetBlackboardAsset())
	{
		// 【UE 5.6 修正】UseBlackboard 必须同时传入黑板资产和黑键组件。
		UseBlackboard(BBAsset, BB);
	}

	// 巡逻初值：家点用出生位置，目标点先给当前位置，避免第一帧找点前为空。
	BB->SetValueAsVector(TEXT("HomeLocation"), Boss->HomeLocation);
	BB->SetValueAsVector(TEXT("PatrolLocation"), Boss->GetActorLocation());

	RunBehaviorTree(BossBehaviorTree);
}
```

讲解：

- `Blackboard` / `BrainComponent` 是 `AAIController` 自带的组件指针，构造函数里用 `CreateDefaultSubobject` 替换成我们想要的类型，这是行为树控制器的标准做法；
- `UseBlackboard`：让黑键组件载入行为树指定的黑板资产。必须在写键值之前调用；
- 黑板键名是 `FName` 字符串，**必须和后面 `BB_Boss` 资产里的键名完全一致**，这是最容易出错的地方；
- `RunBehaviorTree` 在初值写好之后再启动，巡逻任务第一次执行时就能读到正确的家点。

---

## 7. 第四步：创建 UBTTask_FindPatrolLocation

### 7.1 用编辑器向导创建类

1. **工具（Tools）→ 新建 C++ 类（New C++ Class）** → 勾选 **显示所有类（Show All Classes）**；
2. 搜索父类 `BTTaskNode`（即 `UBTTaskNode`）；
3. 类名填写 `BTTask_FindPatrolLocation`；
4. 创建后把文件放到 `Source/First/Public/AI/Tasks` 与 `Source/First/Private/AI/Tasks`（手动创建这两个目录），再把下面的完整内容整体替换进去（**新建文件，内容全部为新增**，改动位置见 7.2 / 7.3 开头）。

### 7.2 头文件

> 【改动位置】文件 `BTTask_FindPatrolLocation.h`，类声明
> 【新增内容】`HomeLocationKey`、`PatrolLocationKey` 两个黑板键属性；`ExecuteTask()` 声明

`Source/First/Public/AI/Tasks/BTTask_FindPatrolLocation.h`：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_FindPatrolLocation.generated.h"

/**
 * 在 BOSS 家点周围的巡逻半径内，找一个 NavMesh 可达的随机点写入黑板。
 *
 * 找不到可达点时回退到“家点”，保证 BOSS 至少有一个可移动的目的地。
 */
UCLASS()
class FIRST_API UBTTask_FindPatrolLocation : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_FindPatrolLocation();

	// 家点所在的黑板键。
	UPROPERTY(EditAnywhere, Category="Boss|Patrol")
	FName HomeLocationKey = TEXT("HomeLocation");

	// 本次巡逻目标点要写入的黑板键。
	UPROPERTY(EditAnywhere, Category="Boss|Patrol")
	FName PatrolLocationKey = TEXT("PatrolLocation");

	virtual EBTNodeResult::Type ExecuteTask(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory) override;
};
```

### 7.3 源文件

> 【改动位置】文件 `BTTask_FindPatrolLocation.cpp`，`ExecuteTask()`
> 【新增内容】完整实现：读家点 → 随机可达点（UNavigationSystemV1）→ 写 `PatrolLocation` 黑键

`Source/First/Private/AI/Tasks/BTTask_FindPatrolLocation.cpp`：

```cpp
#include "AI/Tasks/BTTask_FindPatrolLocation.h"

#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Character/BossCharacter.h"
#include "NavigationSystem.h"

UBTTask_FindPatrolLocation::UBTTask_FindPatrolLocation()
{
	NodeName = TEXT("Find Patrol Location");
}

EBTNodeResult::Type UBTTask_FindPatrolLocation::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	AAIController* AIController = OwnerComp.GetAIOwner();
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	ABossCharacter* Boss = AIController
		? Cast<ABossCharacter>(AIController->GetPawn())
		: nullptr;

	if (!AIController || !BB || !Boss)
	{
		return EBTNodeResult::Failed;
	}

	const FVector HomeLocation = BB->GetValueAsVector(HomeLocationKey);
	FVector OutLocation = HomeLocation;

	// 【UE 5.6 修正】随机可达点查询在 UNavigationSystemV1 上，不在 UAIBlueprintHelperLibrary。
	// 在 NavMesh 上取家点半径内的随机可达点；
	// 失败（例如没有 NavMesh）时保持回退点 = 家点，任务仍然成功。
	UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetCurrent(Boss->GetWorld());
	FNavLocation NavLocation;
	if (NavSystem && NavSystem->GetRandomReachablePointInRadius(
		HomeLocation,
		Boss->PatrolRadius,
		NavLocation))
	{
		OutLocation = NavLocation.Location;
	}

	BB->SetValueAsVector(PatrolLocationKey, OutLocation);
	return EBTNodeResult::Succeeded;
}
```

讲解：

- 找点不涉及等待，所以 `ExecuteTask` 直接返回 `Succeeded`，不需要 `FinishLatentTask`；
- `GetRandomReachablePointInRadius` 是 `UNavigationSystemV1` 提供的“半径内随机可达点”查询。UE 5.6 中它不再放在 `UAIBlueprintHelperLibrary` 上，需要 include `NavigationSystem.h` 并通过 `GetCurrent()` 取导航系统实例；
- 返回结果是 `FNavLocation`（NavMesh 上的位置），成功后用 `NavLocation.Location` 作为目标点；
- 它需要地图里有 NavMesh（NavMeshBoundsVolume），否则永远返回失败，Boss 会一直走向家点；
- 巡逻点数据放在黑板键 `PatrolLocation`，`Move To` 节点读取同一个键，两端键名必须一致。

---

## 8. 第五步：编译

按顺序编译，每完成一步做一次普通 Build（不要只依赖 Live Coding）：

```text
闸门 1：First.Build.cs 加入 AIModule / NavigationSystem
闸门 2：ABossCharacter
闸门 3：ABossAIController
闸门 4：UBTTask_FindPatrolLocation
```

常见编译错误：

| 报错 | 原因 | 修复 |
|---|---|---|
| 找不到 `AAIController` / `UBTTaskNode` | 没加 AIModule | 检查第 4 步 |
| `GetRandomReachablePointInRadius` 不是 `UAIBlueprintHelperLibrary` 的成员 | UE 5.6 中该函数在 `UNavigationSystemV1` 上 | 按 7.3 的写法改用 `UNavigationSystemV1::GetCurrent` + `GetRandomReachablePointInRadius` |
| `BossCharacter.generated.h` 找不到 | 类名与文件名不一致 | 确认类名就是 `BossCharacter` |

---

## 9. 第六步：编辑器资产配置

### 9.1 创建 BB_Boss（黑板）

1. 内容浏览器空白处右键 → **人工智能（AI）→ 黑板（Blackboard）**，命名 `BB_Boss`；
2. 添加两个键：

| 键名 | 类型 | 说明 |
|---|---|---|
| `HomeLocation` | Vector | 巡逻圆心（家点） |
| `PatrolLocation` | Vector | 当前巡逻目标点 |

键名必须和 C++ 里的 `FName` 完全一致（大小写、拼写都要对）。

### 9.2 创建 BT_Boss（行为树）

1. 右键 → **人工智能（AI）→ 行为树（Behavior Tree）**，命名 `BT_Boss`；
2. 选中根节点（行为树根 / Root），在**细节（Details）**面板把 **黑板资产（Blackboard Asset）** 设为 `BB_Boss`；
3. 右键根节点添加子节点，搭出下面的结构：

```text
BT_Boss（根 = 选择器 Selector）
└─ 巡逻分支（序列 Sequence）
   1. Find Patrol Location（我们的 C++ 任务）
   2. 移至（Move To）
        黑板键（Blackboard Key）= PatrolLocation
        可接受半径（Acceptable Radius）= 60
        重叠时停止（Stop On Overlap）= 关闭
   3. 等待（Wait）
        等待时间（Wait Time）= 2.0
```

说明：

- 行为树执行完一轮后会重新评估，序列（Sequence）会自动循环成“找点 → 移动 → 等待”；
- 等待（Wait）节点的 2 秒就是“到达巡逻点后的停留时间”，数值直接在编辑器里调；
- 可接受半径（Acceptable Radius）= 60 表示离目标点 60 以内就算到达，避免站在点上抖动。
- ⚠️ **结构千万别摆错**：必须先有“序列（Sequence）”这一层，三个任务要放在**序列里面**。如果直接把 Find Patrol Location / Move To / Wait 挂在选择器（Selector）下面，选择器只会执行“第一个成功”的节点——Find Patrol Location 瞬间成功，Move To 永远轮不到执行，BOSS 就永远不动。判断方法：看节点连线是不是“选择器 → 序列 → 三个任务”。

### 9.3 创建 BP_BossAIController 与 BP_Boss

为什么需要两个蓝图：

```text
行为树资产（BT_Boss）是配在“AI 控制器”上的，不是配在 BOSS 角色上的；
而原生类 ABossAIController 的默认值不能在 BP_Boss 里直接改，
所以必须再做一个控制器蓝图 BP_BossAIController。
```

#### 第一步：创建 BP_BossAIController（最重要，BOSS 不动的头号原因）

1. 内容浏览器右键 → **蓝图类** → 搜索并选择父类 `BossAIController`（即 `ABossAIController`），命名 `BP_BossAIController`；
2. 打开蓝图，点击工具栏 **类默认值（Class Defaults）**；
3. 在类别 **Boss | AI** 下找到 **BossBehaviorTree**，选择 `BT_Boss`；
4. 点击工具栏 **编译（Compile）**，然后 **保存（Save）**。

#### 第二步：创建并配置 BP_Boss

1. 右键 → **蓝图类** → 搜索并选择父类 `BossCharacter`，命名 `BP_Boss`；
2. 打开蓝图，类默认值（Class Defaults）里配置：

| 属性（中文版显示） | 值 |
|---|---|
| AI 控制器类（AIController Class） | `BP_BossAIController` |
| 自动控制 AI（Auto Possess AI） | 已放置于世界或生成时（Placed in World or Spawned） |
| Character Start Up Data | `DA_BossStartUpData`（见 9.4） |
| 网格体（Mesh） | 先复用 `SK_DKMannequin`，动画类（Anim Class）用 `ABP_DK_Sword` |
| 角色移动（Character Movement）→ 最大行走速度（Max Walk Speed） | 500 |

3. 点击 **编译** 和 **保存**。

先复用现有角色外观来验证 AI 行为，BOSS 专属外观放到后续阶段。

> ⚠️ **最常见错误**：AIController Class 填了原生 `BossAIController`，但 `BossBehaviorTree` 从来没被赋值。结果就是：行为树根本没运行，BOSS 站在原地不动，日志里出现 `[BossAI] 缺少 BOSS 角色、黑键组件或行为树资产`。务必按第一步把 `BT_Boss` 配进 `BP_BossAIController`。

### 9.4 给 BOSS 准备属性（GE_Boss_Initialize 与 DA_BossStartUpData 详细步骤）

目的：让 BOSS 拥有生命、攻击、防御属性，之后挨打、攻击才有数值基础。本册巡逻不依赖属性，但建议现在就配好。

#### 9.4.1 复制并修改 GE_Boss_Initialize

1. 内容浏览器打开 `Content/Enemy/GAS/`，找到已有的 `GE_Enemy_Initialize`；
2. 右键 `GE_Enemy_Initialize` → **复制（Duplicate）**，重命名为 `GE_Boss_Initialize`；
   - 复制而不是从零新建，可以保留正确的效果结构，初学者少踩坑；
3. 双击打开 `GE_Boss_Initialize`；
4. 在细节面板找到 **修饰符（Modifiers）** 数组（在"游戏效果"分类下）；
5. 参考数值（先照抄，以后自己调）：

| 顺序 | 属性（Attribute） | 修饰符操作 | 数值 |
|---|---|---|---|
| 第 1 个 | `MaxHealth` | 加（Additive） | 500 |
| 第 2 个 | `Health` | 加（Additive） | 500 |
| 第 3 个 | `AttackPower` | 加（Additive） | 15 |
| 第 4 个 | `DefensePower` | 加（Additive） | 10 |

   - 属性名在下拉列表里显示为 `Health`、`MaxHealth`、`AttackPower`、`DefensePower`（来自 C++ 的 `FirstAttributeSet`）；
   - 每个元素的设置：**属性（Attribute）** = 对应属性；**修饰符操作（ModifierOp）** = 加（Add）；**修饰符量级（ModifierMagnitude）** = 标量浮点（Scalable Float）并填数值；
   - 改数值的位置：展开该修饰符行 → **修饰符幅度（Modifier Magnitude）** → **可扩展浮点幅度（Scalable Float Magnitude）** 的数值框（例如把 100.0 改成 500）；
   - 修饰符操作用"加（Add）"或"重载（Override）"都行：出生属性用"重载"更常见，如果复制过来的 GE 显示的是"重载（Override）"，不需要改；
6. ⚠️ **顺序提醒：`MaxHealth` 必须排在 `Health` 前面**。你的属性集在每次修改后会把 Health 夹紧到 `[0, MaxHealth]`；如果 Health 先执行，当时 MaxHealth 还是旧值，Health 会被压成 1；
7. 保存 `GE_Boss_Initialize`。

#### 9.4.2 创建 DA_BossStartUpData

1. 内容浏览器进入 `Content/Enemy/Data/`；
2. 空白处右键 → **杂项（Miscellaneous）→ 数据资产（Data Asset）**；
   - 弹出的选择框里搜索 `DataAsset_DKStartUpData` 并选中（和 DK 用同一个类即可，BOSS 用不到的数组留空）；
3. 命名 `DA_BossStartUpData`，双击打开；
4. 找到 **StartUpGameplayEffects** 数组，点击 **+** 添加一项，选择 `GE_Boss_Initialize`；
5. 保存。

#### 9.4.3 把 DA_BossStartUpData 填到 BP_Boss

1. 打开 `BP_Boss` → **类默认值**；
2. 找到 **Character Start Up Data**（C++ 里定义的属性，编辑器显示这个英文名），选择 `DA_BossStartUpData`；
3. 编译并保存。

#### 9.4.4 验证属性生效

1. 运行 PIE；
2. 打开输出日志，应能看到类似 `[BP_Boss_...] ASC initialized` 的日志；
3. 在运行画面按 ` 打开控制台，输入 `ShowDebug AbilitySystem`；
4. 查看视口里 BOSS 的 `Health / MaxHealth` 是否为 500（`AttackPower` 15、`DefensePower` 10）。

本册巡逻不依赖属性也能跑，但 BOSS 迟早要挨打、攻击，属性现在就位更省事。

### 9.5 地图与 NavMesh

1. 打开测试地图，把 `BP_Boss` 拖进场景；
2. 从放置面板（左侧“放置”选项卡）添加 **导航网格边界体积（Nav Mesh Bounds Volume）**，调整大小覆盖 BOSS 巡逻范围；
3. 运行时按 `P` 键可以显示导航网格（绿色区域），确认它覆盖了地图。

没有 NavMesh 时，`MoveTo` 永远失败。注意：

- 导航网格边界体积放好后，运行时会**自动生成**导航网格，不需要手动点构建；
- 如果 BOSS 出生在绿色区域边缘，`MoveTo` 也可能失败，把它拖到网格中间再测；
- ⚠️ **你的 Lvl_ThirdPerson 是 World Partition（世界分区）地图**：这种地图里，NavMeshBoundsVolume 要放在**持久关卡（Persistent Level）**。怎么判断：打开大纲（Outliner），如果 NavMeshBoundsVolume **直接挂在最顶层的关卡根节点（例如 `Lvl_ThirdPerson（编辑器）`）下面**，就说明已经在持久关卡了，不用动；如果它挂在"世界分区/数据层"之类的分组里，才需要把它拖到根节点下并保存地图。否则运行时可能找不到导航网格，日志会出现：

```text
LogCrowdFollowing: Warning: Unable to find RecastNavMesh instance while trying to create UCrowdManager instance
```

看到这条日志 = 导航网格没有生成，先解决它再测巡逻。

- ⚠️ **如果 PIE 运行时按 P，绿色网格浮在头顶（例如贴在围墙顶端）而不是贴在地板**：说明运行时生成的导航网格没有覆盖角色脚下那一层，`MoveTo` 自然失败。按顺序排查（前面已经确认体积在持久关卡、角色和地板同层）：

  1. **运行时生成模式改为"动态"**（最常见，尤其 World Partition 地图）：菜单 → **项目设置 → 导航系统（Navigation System）→ 运行时生成（Runtime Generation）**，从"静态（Static）"改为**动态（Dynamic）**，保存后重新运行 PIE，等 5～10 秒再按 P。原因：静态模式只在开局生成一次网格，如果地板是流送加载的、比网格生成晚一步，它就不会被盖到；
  2. **检查地板是否"可移动"**：选中竞技场地板，细节面板 → Transform → **移动性（Mobility）**必须是**静态（Static）**。可移动网格不会参与静态导航生成；
  3. **体积别把地板卡在边界**：选中 NavMeshBoundsVolume，位置 Z 设 **0～30**、缩放 Z 设 **2～3**，保证地板（Z=0）在体积内部且留出余量，保存。

---

## 10. 运行时完整流程

```text
BP_Boss 被 AIController Possess
→ OnPossess → 黑板写入 HomeLocation / PatrolLocation
→ RunBehaviorTree(BT_Boss)
→ 巡逻分支
   → Find Patrol Location：HomeLocation 半径 1200 内随机可达点 → PatrolLocation
   → Move To：走向 PatrolLocation（MaxWalkSpeed 500）
   → 到达（距离 < 60）→ Wait 2 秒
→ 重新找下一个点，循环
```

---

## 11. 测试清单

| 测试 | 操作 | 预期结果 |
|---|---|---|
| 出生巡逻 | 运行地图，观察 BOSS | 出生后自动开始向某个方向移动 |
| 不超出范围 | 观察 1～2 分钟 | BOSS 始终在家点 1200 半径内活动 |
| 到达等待 | 跟踪 BOSS 到目标点 | 停下约 2 秒后再去下一个点 |
| 绕开障碍 | 在路径上摆障碍物 | BOSS 沿 NavMesh 绕行（依赖导航网格） |
| 不理会远处玩家 | 玩家站在警戒范围外 | BOSS 继续巡逻（索敌下一册实现） |
| 重复稳定 | 多次 Start/Stop PIE | 每次出生后都能正常巡逻 |

---

## 12. 常见问题与排查

### 12.1 BOSS 不动时的排查顺序（必看）

按下面的顺序一项一项查，查完基本能找到原因：

```text
第 1 步：编译是否通过
  行为树里能找到 Find Patrol Location 节点 = C++ 编译成功。
  如果找不到这个节点，先解决编译问题。

第 2 步：BossBehaviorTree 有没有配置（最常见原因）
  打开 BP_BossAIController → 类默认值 → Boss | AI → BossBehaviorTree = BT_Boss。
  没配置的话，运行时输出日志会出现：
  [BossAI] 缺少 BOSS 角色、黑键组件或行为树资产

第 3 步：运行 PIE 后打开输出日志
  点编辑器底部“输出日志”，搜索 BossAI，看有没有上面的报错。
  注意：PIE 至少跑 5 秒以上再判断。运行时导航网格需要一点时间生成，
  Move To 在最开始的一瞬间失败是正常的，不代表配置错误。

第 4 步：看导航网格
  运行中按 P，确认 BOSS 出生位置和目标区域是绿色。
  没有绿色 = NavMeshBoundsVolume 没覆盖到，或体积太小。
  运行（PIE）中再按一次 P：运行时也要有绿色网格。
  先不运行、直接在编辑器里按 P 看一次：
  - 编辑器里也没绿色 → 体积没放对/太小/不在持久关卡；
  - 编辑器里有绿色、运行后没有 → 多半是 World Partition 流送问题，
    把体积拖进 Persistent Level 并保存地图。
  日志证据：LogCrowdFollowing: Warning: Unable to find RecastNavMesh instance

第 5 步：看行为树调试器
  保持 BT_Boss 窗口打开，PIE 运行后，行为树工具栏会出现调试器下拉框，
  选择 BOSS 的 AI 控制器，就能看到当前正在执行哪个节点：
  - Find Patrol Location 红色失败 → 导航网格或黑板键问题；
  - Move To 一直不成功 → 目标点不可达（导航网格）；
  - 整个行为树都没执行 → 问题在 BossBehaviorTree / AutoPossessAI。

第 6 步：检查控制权
  运行后选中场景里的 BOSS，确认它被 AI 控制器占有（Possessed）。
  或在控制台输入 ShowDebug AI，看控制器状态。

第 7 步：检查移动与碰撞
  BP_Boss → 类默认值 → 角色移动 → 最大行走速度 > 0；
  胶囊体没有被卡住（例如半截埋进地板）；bOrientRotationToMovement 已开启。
```

### 12.2 常见问题速查表

| 现象 | 原因 | 修复 |
|---|---|---|
| BOSS 出生后不动 | 最常见：BossBehaviorTree 没配置；其次是 NavMesh / AutoPossessAI / AIControllerClass | 按 12.1 从第 2 步开始查 |
| 一直在原地打转 | 没有 NavMesh，或目标点与自身过近 | 检查导航网格；可把 Acceptable Radius 调大 |
| 跑出巡逻范围 | PatrolRadius 太大或家点不对 | 检查 `HomeLocation` 黑键值与半径 |
| 黑板相关报错/断言 | 键名不一致 | 对照 9.1 的键名表逐字核对 |
| 编译找不到模块 | Build.cs 没加模块 | 按第 4 步 |
| 移动速度不对 | MaxWalkSpeed 被其他地方覆盖 | 确认 `BP_Boss` 里角色移动组件的最大行走速度（Max Walk Speed） |

---

## 13. 后续扩展

- **巡逻点数组（决策书 D1 方案）**：黑板加 `PatrolPoints`（Object 数组）和 `PatrolIndex`，任务按下标取点，适合固定路线巡逻；
- **巡逻/追击双速度**：黑板加 Float 键 `MoveSpeed`，进入追击时把 `MaxWalkSpeed` 改大；
- **固定战场的家点**：`bUseSpawnLocationAsHome = false`，手动填 HomeLocation；
- **下一步**：视觉索敌、三范围切换、警戒与追击，见《BOSS 索敌与警戒实现指导》。

---

## 14. UE 5.6 修正对照（初学者按这个找位置）

以下两处是 UE 5.6 与旧版引擎不同的 API。本文里的代码已经修正；如果你的文件是按旧版本抄的，按下面的表逐字改。

### 14.1 修正 1：UseBlackboard（BossAIController.cpp）

- 文件：`Source/First/Private/Controllers/BossAIController.cpp`
- 函数：`InitializeBossBlackboard()`
- 搜索关键词：`UseBlackboard`

旧写法（编译报错：不接受 1 个参数）：

```cpp
UseBlackboard(BBAsset);
```

新写法（UE 5.6 必须同时给黑板资产和黑键组件）：

```cpp
// 【UE 5.6 修正】UseBlackboard 必须同时传入黑板资产和黑键组件。
UseBlackboard(BBAsset, BB);
```

### 14.2 修正 2：随机巡逻点查询（BTTask_FindPatrolLocation.cpp）

- 文件：`Source/First/Private/AI/Tasks/BTTask_FindPatrolLocation.cpp`
- 函数：`ExecuteTask()`
- 搜索关键词：`GetRandomReachablePointInRadius`

① 文件顶部的 include 区：删掉这一行

```cpp
#include "Blueprint/AIBlueprintHelperLibrary.h"
```

换成：

```cpp
#include "NavigationSystem.h"
```

② 函数中间的找点代码，旧写法（编译报错：不是 `UAIBlueprintHelperLibrary` 的成员）：

```cpp
UAIBlueprintHelperLibrary::GetRandomReachablePointInRadius(
	Boss,
	HomeLocation,
	Boss->PatrolRadius,
	OutLocation);
```

换成新写法：

```cpp
// 【UE 5.6 修正】随机可达点查询在 UNavigationSystemV1 上。
UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetCurrent(Boss->GetWorld());
FNavLocation NavLocation;
if (NavSystem && NavSystem->GetRandomReachablePointInRadius(
	HomeLocation,
	Boss->PatrolRadius,
	NavLocation))
{
	OutLocation = NavLocation.Location;
}
```

改完这两处后，做一次普通 Build。
