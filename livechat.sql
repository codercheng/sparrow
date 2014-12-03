CREATE DATABASE chatmessage;

CREATE TABLE message (
	mid BIGINT NOT NULL AUTO_INCREMENT,
	mtime VARCHAR(16) NOT NULL,
	mbody TEXT NOT NULL,
	PRIMARY KEY (mid)
	);

INSERT INTO chatmessage.message VALUES(NULL, sql, );


create user simon IDENTIFIED by '135016';
grant select, insert on chatmessage.message TO 'forchat'@'%';
