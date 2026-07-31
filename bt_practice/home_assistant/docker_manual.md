아래 내용을 그대로 복사해서 `docker_homeassistant_mosquitto_commands.md`로 저장하시면 됩니다.

```markdown
# Raspberry Pi Docker 명령 정리

## 1. 현재 구성

사용 중인 환경:

- Raspberry Pi 5
- Debian 12
- Docker Engine
- Docker Compose
- Home Assistant Container
- Mosquitto MQTT Broker

주요 폴더:

```text
~/docker/homeassistant
~/docker/mosquitto
```

각 폴더 안에는 보통 다음 파일이 있습니다.

```text
compose.yaml
```

또는:

```text
docker-compose.yml
```

Docker Compose 명령은 해당 파일이 있는 폴더에서 실행하는 것이 가장 안전합니다.

---

# 2. Docker 전체 상태 확인

Docker 서비스 상태 확인:

```bash
sudo systemctl status docker
```

Docker 시작:

```bash
sudo systemctl start docker
```

Docker 중지:

```bash
sudo systemctl stop docker
```

Docker 재시작:

```bash
sudo systemctl restart docker
```

부팅 시 자동 시작 설정:

```bash
sudo systemctl enable docker
```

자동 시작 해제:

```bash
sudo systemctl disable docker
```

Docker 기본 정보 확인:

```bash
docker info
```

Docker 버전 확인:

```bash
docker version
```

Compose 버전 확인:

```bash
docker compose version
```

---

# 3. 실행 중인 컨테이너 확인

현재 실행 중인 컨테이너만 확인:

```bash
docker ps
```

정지된 컨테이너까지 모두 확인:

```bash
docker ps -a
```

상태 해석:

```text
Up          실행 중
Exited      정지 상태
목록에 없음  컨테이너가 삭제된 상태
```

---

# 4. Home Assistant 관리

Home Assistant 폴더로 이동:

```bash
cd ~/docker/homeassistant
```

현재 상태 확인:

```bash
docker compose ps
```

## 실행

컨테이너가 없으면 생성하고 실행합니다.

이미 정지된 컨테이너가 있으면 기존 컨테이너를 시작합니다.

```bash
docker compose up -d
```

`-d`는 백그라운드 실행을 의미합니다.

## 정지

컨테이너를 삭제하지 않고 정지만 합니다.

```bash
docker compose stop
```

다시 시작:

```bash
docker compose start
```

재시작:

```bash
docker compose restart
```

## 컨테이너 삭제

컨테이너와 Compose 네트워크를 삭제합니다.

```bash
docker compose down
```

다시 실행하려면:

```bash
docker compose up -d
```

## 로그 확인

실시간 로그:

```bash
docker compose logs -f
```

최근 100줄만 확인:

```bash
docker compose logs --tail 100
```

Home Assistant 서비스 로그만 확인:

```bash
docker compose logs -f homeassistant
```

실시간 로그 종료:

```text
Ctrl + C
```

로그 확인을 종료해도 컨테이너는 계속 실행됩니다.

## Home Assistant 컨테이너 내부 접속

```bash
docker compose exec homeassistant bash
```

나오기:

```bash
exit
```

컨테이너 내부에서 설정 폴더 확인:

```bash
ls -al /config
```

---

# 5. Mosquitto 관리

Mosquitto 폴더로 이동:

```bash
cd ~/docker/mosquitto
```

상태 확인:

```bash
docker compose ps
```

실행:

```bash
docker compose up -d
```

정지:

```bash
docker compose stop
```

다시 시작:

```bash
docker compose start
```

재시작:

```bash
docker compose restart
```

컨테이너 삭제:

```bash
docker compose down
```

로그 확인:

```bash
docker compose logs -f
```

최근 로그 확인:

```bash
docker compose logs --tail 100
```

Mosquitto 컨테이너 내부 접속:

```bash
docker compose exec mosquitto sh
```

이미지에 Bash가 설치되어 있으면:

```bash
docker compose exec mosquitto bash
```

---

# 6. Home Assistant와 Mosquitto 함께 시작

Home Assistant 시작:

```bash
cd ~/docker/homeassistant
docker compose up -d
```

Mosquitto 시작:

```bash
cd ~/docker/mosquitto
docker compose up -d
```

전체 실행 상태 확인:

```bash
docker ps
```

---

# 7. Home Assistant와 Mosquitto 함께 중지

잠시 정지할 때:

```bash
cd ~/docker/homeassistant
docker compose stop

cd ~/docker/mosquitto
docker compose stop
```

다시 시작:

```bash
cd ~/docker/homeassistant
docker compose start

cd ~/docker/mosquitto
docker compose start
```

컨테이너까지 삭제할 때:

```bash
cd ~/docker/homeassistant
docker compose down

cd ~/docker/mosquitto
docker compose down
```

---

# 8. stop과 down 차이

## stop

```bash
docker compose stop
```

의미:

```text
컨테이너 유지
컨테이너 정지
이미지 유지
데이터 유지
```

다시 실행:

```bash
docker compose start
```

또는:

```bash
docker compose up -d
```

## down

```bash
docker compose down
```

의미:

```text
컨테이너 삭제
Compose 네트워크 삭제
이미지 유지
외부 저장 데이터는 보통 유지
```

다시 실행:

```bash
docker compose up -d
```

그러면 새로운 컨테이너가 생성됩니다.

---

# 9. up -d 동작

```bash
docker compose up -d
```

동작:

```text
compose.yaml 확인
↓
컨테이너 존재 여부 확인
↓
없으면 생성
있고 정지 상태면 시작
설정이 변경됐으면 필요에 따라 재생성
↓
백그라운드 실행
```

`docker compose start`는 기존 컨테이너가 반드시 있어야 합니다.

반면 `docker compose up -d`는 컨테이너가 없어도 새로 생성합니다.

---

# 10. 데이터가 유지되는 이유

Home Assistant Compose 설정 예:

```yaml
services:
  homeassistant:
    image: ghcr.io/home-assistant/home-assistant:stable
    volumes:
      - ./config:/config
```

의미:

```text
Raspberry Pi
~/docker/homeassistant/config

        ↓ 연결

Home Assistant 컨테이너
/config
```

따라서:

```bash
docker compose down
```

으로 컨테이너를 삭제해도 다음 호스트 폴더는 남습니다.

```text
~/docker/homeassistant/config
```

새 컨테이너를 만들면 이 폴더를 다시 `/config`에 연결합니다.

---

# 11. 주의해야 할 삭제 명령

## 컨테이너만 삭제

```bash
docker compose down
```

일반적으로 bind mount 데이터는 유지됩니다.

## Docker 볼륨까지 삭제

```bash
docker compose down -v
```

주의:

```text
컨테이너 삭제
네트워크 삭제
Compose가 만든 named volume 삭제
```

데이터베이스나 설정이 named volume에 저장되어 있다면 사라질 수 있습니다.

## 이미지까지 삭제

```bash
docker compose down --rmi all
```

다시 실행하면 이미지를 다시 다운로드해야 합니다.

## 사용하지 않는 리소스 정리

```bash
docker system prune
```

더 강한 정리:

```bash
docker system prune -a
```

이 명령들은 사용하지 않는 컨테이너, 네트워크, 이미지 등을 삭제하므로 내용을 확인한 뒤 사용합니다.

볼륨까지 정리하는 명령은 특히 주의합니다.

```bash
docker system prune --volumes
```

---

# 12. 이미지 확인

설치된 Docker 이미지 확인:

```bash
docker images
```

또는:

```bash
docker image ls
```

Home Assistant 이미지 확인:

```bash
docker images | grep home-assistant
```

Mosquitto 이미지 확인:

```bash
docker images | grep mosquitto
```

이미지 상세 정보:

```bash
docker image inspect ghcr.io/home-assistant/home-assistant:stable
```

Mosquitto 이미지 상세 정보:

```bash
docker image inspect eclipse-mosquitto
```

---

# 13. 컨테이너 상세 정보

Home Assistant 컨테이너 확인:

```bash
docker inspect homeassistant
```

Mosquitto 컨테이너 확인:

```bash
docker inspect mosquitto
```

컨테이너 이름을 모르면:

```bash
docker ps -a
```

마운트 정보만 확인:

```bash
docker inspect homeassistant \
  --format '{{json .Mounts}}'
```

이미지 이름 확인:

```bash
docker inspect homeassistant \
  --format '{{.Config.Image}}'
```

컨테이너 실행 상태 확인:

```bash
docker inspect homeassistant \
  --format '{{.State.Status}}'
```

---

# 14. Docker 이미지 업데이트

## Home Assistant 업데이트

```bash
cd ~/docker/homeassistant
docker compose pull
docker compose up -d
```

동작:

```text
새 이미지 다운로드
↓
기존 설정 확인
↓
필요하면 컨테이너 재생성
↓
기존 config 폴더 다시 연결
```

업데이트 후 로그 확인:

```bash
docker compose logs -f --tail 100
```

## Mosquitto 업데이트

```bash
cd ~/docker/mosquitto
docker compose pull
docker compose up -d
```

로그 확인:

```bash
docker compose logs -f --tail 100
```

---

# 15. 안전한 업데이트 순서

Home Assistant 업데이트 전 설정 백업:

```bash
cd ~/docker/homeassistant
cp -a config "config_backup_$(date +%Y%m%d_%H%M%S)"
```

업데이트:

```bash
docker compose pull
docker compose up -d
```

상태 확인:

```bash
docker compose ps
docker compose logs --tail 100
```

Mosquitto도 설정 폴더가 있다면 백업합니다.

예:

```bash
cd ~/docker/mosquitto
cp -a config "config_backup_$(date +%Y%m%d_%H%M%S)"
```

---

# 16. MQTT 테스트 명령

## 구독

로컬 Mosquitto의 모든 토픽 구독:

```bash
mosquitto_sub \
  -h localhost \
  -t '#' \
  -v
```

특정 토픽 구독:

```bash
mosquitto_sub \
  -h localhost \
  -t home/livingroom/fan/state \
  -v
```

## 발행

전원 켜기 명령:

```bash
mosquitto_pub \
  -h localhost \
  -t home/livingroom/fan/power/set \
  -m ON
```

전원 끄기:

```bash
mosquitto_pub \
  -h localhost \
  -t home/livingroom/fan/power/set \
  -m OFF
```

상태 발행:

```bash
mosquitto_pub \
  -h localhost \
  -t home/livingroom/fan/power/state \
  -m ON
```

장치 온라인 상태 발행:

```bash
mosquitto_pub \
  -h localhost \
  -t home/livingroom/fan/status \
  -m online \
  -r
```

`-r`은 retained 메시지로 저장하라는 의미입니다.

## 다른 PC에서 접속

```bash
mosquitto_sub \
  -h 라즈베리파이_IP주소 \
  -t '#' \
  -v
```

예:

```bash
mosquitto_sub \
  -h 192.168.0.10 \
  -t '#' \
  -v
```

---

# 17. MQTT retained 메시지

Retained 메시지 발행:

```bash
mosquitto_pub \
  -h localhost \
  -t home/livingroom/fan/status \
  -m online \
  -r
```

Retained 메시지 삭제:

```bash
mosquitto_pub \
  -h localhost \
  -t home/livingroom/fan/status \
  -n \
  -r
```

`-n`은 빈 payload를 전송합니다.

---

# 18. 임시 컨테이너 실행

Ubuntu 셸을 잠깐 실행:

```bash
docker run --rm -it ubuntu:24.04 bash
```

Alpine Linux 정보 확인:

```bash
docker run --rm alpine cat /etc/os-release
```

Linux 커널 확인:

```bash
docker run --rm alpine uname -a
```

CPU 아키텍처 확인:

```bash
docker run --rm alpine uname -m
```

`--rm` 의미:

```text
컨테이너 종료 후 자동 삭제
이미지는 유지
컨테이너 내부 변경 사항은 삭제
```

---

# 19. 컨테이너 내부 파일 유지 여부

같은 컨테이너를 정지 후 다시 시작:

```bash
docker stop 컨테이너이름
docker start 컨테이너이름
```

컨테이너 내부 파일은 유지됩니다.

컨테이너를 삭제:

```bash
docker rm 컨테이너이름
```

컨테이너 내부에만 있던 파일은 함께 삭제됩니다.

중요한 데이터는 다음 중 하나에 저장해야 합니다.

```text
Bind mount
Docker named volume
호스트 폴더
```

---

# 20. 직접 생성한 컨테이너 관리

이름을 지정해 Ubuntu 실행:

```bash
docker run -it \
  --name myubuntu \
  ubuntu:24.04 \
  bash
```

종료:

```bash
exit
```

정지된 컨테이너 다시 실행:

```bash
docker start -ai myubuntu
```

컨테이너 삭제:

```bash
docker rm myubuntu
```

강제 삭제:

```bash
docker rm -f myubuntu
```

---

# 21. 로그와 문제 확인

실행 상태 확인:

```bash
docker ps
```

정지된 컨테이너 포함:

```bash
docker ps -a
```

Home Assistant 로그:

```bash
cd ~/docker/homeassistant
docker compose logs --tail 200
```

Mosquitto 로그:

```bash
cd ~/docker/mosquitto
docker compose logs --tail 200
```

컨테이너가 계속 재시작되는지 확인:

```bash
docker ps
```

상태에 다음이 반복되면 문제가 있을 수 있습니다.

```text
Restarting
```

Docker 서비스 로그 확인:

```bash
sudo journalctl -u docker
```

최근 로그만 확인:

```bash
sudo journalctl -u docker -n 100
```

실시간 확인:

```bash
sudo journalctl -u docker -f
```

---

# 22. 포트 확인

현재 Docker 컨테이너 포트 확인:

```bash
docker ps
```

특정 컨테이너 포트 확인:

```bash
docker port homeassistant
```

Home Assistant 기본 접속 주소:

```text
http://라즈베리파이_IP주소:8123
```

Mosquitto 기본 MQTT 포트:

```text
1883
```

포트 사용 상태 확인:

```bash
sudo ss -lntp
```

8123 포트 확인:

```bash
sudo ss -lntp | grep 8123
```

1883 포트 확인:

```bash
sudo ss -lntp | grep 1883
```

---

# 23. 자주 사용하는 명령 모음

## Home Assistant 실행

```bash
cd ~/docker/homeassistant
docker compose up -d
```

## Home Assistant 상태

```bash
cd ~/docker/homeassistant
docker compose ps
```

## Home Assistant 로그

```bash
cd ~/docker/homeassistant
docker compose logs -f
```

## Home Assistant 정지

```bash
cd ~/docker/homeassistant
docker compose stop
```

## Mosquitto 실행

```bash
cd ~/docker/mosquitto
docker compose up -d
```

## Mosquitto 상태

```bash
cd ~/docker/mosquitto
docker compose ps
```

## Mosquitto 로그

```bash
cd ~/docker/mosquitto
docker compose logs -f
```

## 모든 컨테이너 확인

```bash
docker ps -a
```

## 모든 이미지 확인

```bash
docker images
```

---

# 24. 추천 일상 사용 패턴

## 평소 켜기

```bash
cd ~/docker/mosquitto
docker compose up -d

cd ~/docker/homeassistant
docker compose up -d
```

## 평소 상태 확인

```bash
docker ps
```

## 잠시 끄기

```bash
cd ~/docker/homeassistant
docker compose stop

cd ~/docker/mosquitto
docker compose stop
```

## 다시 켜기

```bash
cd ~/docker/mosquitto
docker compose start

cd ~/docker/homeassistant
docker compose start
```

## Compose 설정 변경 후 적용

```bash
docker compose up -d
```

## 업데이트

```bash
docker compose pull
docker compose up -d
```

---

# 25. 가장 중요한 개념

```text
Image
프로그램 실행 원본

Container
이미지로 만든 실행 인스턴스

Volume / Bind mount
컨테이너 삭제 후에도 유지할 데이터 저장소

docker run
이미지로 새 컨테이너 생성 후 실행

docker start
기존 컨테이너 실행

docker compose up -d
Compose 설정에 맞춰 컨테이너 생성 또는 실행

docker compose stop
컨테이너를 유지하면서 정지

docker compose down
컨테이너 삭제

--rm
컨테이너 종료 시 자동 삭제
```

---

# 26. 삭제 전 확인 사항

다음 명령을 실행하기 전에는 반드시 데이터 위치를 확인합니다.

```bash
docker compose down -v
docker volume rm
docker system prune --volumes
rm -rf ~/docker/homeassistant/config
rm -rf ~/docker/mosquitto
```

특히 다음 폴더는 Home Assistant 설정과 데이터가 들어 있을 수 있습니다.

```text
~/docker/homeassistant/config
```

중요한 작업 전 백업:

```bash
cp -a \
  ~/docker/homeassistant/config \
  ~/docker/homeassistant/config_backup
```
```

가장 자주 쓰는 명령만 압축하면 다음 여섯 개입니다.

```bash
docker ps -a
docker compose ps
docker compose up -d
docker compose stop
docker compose start
docker compose logs -f
```
