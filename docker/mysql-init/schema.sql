CREATE TABLE IF NOT EXISTS userinfo (
  id          int          NOT NULL PRIMARY KEY AUTO_INCREMENT,
  mobile      varchar(16)  NOT NULL UNIQUE,
  username    varchar(128) NOT NULL DEFAULT '',
  registertm  timestamp    NOT NULL DEFAULT CURRENT_TIMESTAMP,
  INDEX mobile_index (mobile)
);

CREATE TABLE IF NOT EXISTS account (
  user_id     int          NOT NULL PRIMARY KEY,
  balance     int          NOT NULL DEFAULT 0,
  FOREIGN KEY (user_id) REFERENCES userinfo(id)
);

CREATE TABLE IF NOT EXISTS account_record (
  id            bigint       NOT NULL PRIMARY KEY AUTO_INCREMENT,
  user_id       int          NOT NULL,
  type          tinyint      NOT NULL,
  amount        int          NOT NULL,
  balance_after int          NOT NULL,
  tm            timestamp    NOT NULL DEFAULT CURRENT_TIMESTAMP,
  INDEX user_tm (user_id, tm)
);
