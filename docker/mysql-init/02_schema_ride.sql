-- 02_schema_ride.sql — 骑行闭环扩展表
-- 字母序排在 schema.sql 之后,自动按顺序执行

CREATE TABLE IF NOT EXISTS bike (
  id         int          NOT NULL PRIMARY KEY AUTO_INCREMENT,
  bike_no    varchar(32)  NOT NULL UNIQUE,
  lat        decimal(10,7) NOT NULL,
  lng        decimal(10,7) NOT NULL,
  status     tinyint      NOT NULL DEFAULT 0,
  created_at timestamp    NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at timestamp    NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS ride (
  id            bigint       NOT NULL PRIMARY KEY AUTO_INCREMENT,
  ride_no       varchar(32)  NOT NULL UNIQUE,
  user_id       int          NOT NULL,
  bike_id       int          NOT NULL,
  start_tm      timestamp    NOT NULL,
  end_tm        timestamp    NOT NULL,
  start_lat     decimal(10,7) NOT NULL,
  start_lng     decimal(10,7) NOT NULL,
  end_lat       decimal(10,7) NOT NULL,
  end_lng       decimal(10,7) NOT NULL,
  duration_sec  int          NOT NULL,
  distance_m    int          NOT NULL,
  amount_cent   int          NOT NULL,
  status        tinyint      NOT NULL DEFAULT 0,
  INDEX idx_user_start (user_id, start_tm),
  INDEX idx_bike_start (bike_id, start_tm),
  FOREIGN KEY (user_id) REFERENCES userinfo(id),
  FOREIGN KEY (bike_id) REFERENCES bike(id)
);

CREATE TABLE IF NOT EXISTS ride_position (
  id          bigint       NOT NULL PRIMARY KEY AUTO_INCREMENT,
  ride_id     bigint       NOT NULL,
  seq         int          NOT NULL,
  lat         decimal(10,7) NOT NULL,
  lng         decimal(10,7) NOT NULL,
  elapsed_sec int          NOT NULL,
  INDEX idx_ride_seq (ride_id, seq),
  FOREIGN KEY (ride_id) REFERENCES ride(id)
);
