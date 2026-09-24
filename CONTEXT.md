# JugGfx

D3D11 그래픽스 라이브러리. 렌더 디바이스와, 월드에 놓인 오브젝트의 공간 관계를 다룬다.

## Language

### 공간 관계

**Transform**:
한 오브젝트의 아핀 변환을 Scale / Rotation(쿼터니언) / Translation 세 성분으로 표현한 값.
_Avoid_: AffineTransform, SRT, Pose

**Local**:
부모 기준으로 표현한 Transform. 트리에서 편집의 원본이 되는 쪽.
_Avoid_: Relative

**World**:
루트 기준으로 표현한 최종 변환 행렬. Local 들을 합성한 파생값이며 SRT 가 아니다.
_Avoid_: Global, WorldTransform

**Transform Tree**:
Local 사이의 부모-자식 관계 전체. 루트부터 합성해 각 노드의 World 를 만든다.
_Avoid_: Scene Graph, Hierarchy
