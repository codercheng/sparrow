CREATE TABLE task (
	task_id BIGINT NOT NULL AUTO_INCREMENT,
	task_user_name VARCHAR(20) NOT NULL,
	task_create_time VARCHAR(16) NOT NULL,
	task_delete_time VARCHAR(16),
	task_finish_time VARCHAR(16),
	task_status INT NOT NULL,
	task_content TEXT NOT NULL,
	
	PRIMARY KEY (task_id)
);

//task_status
//1-->not finish 
//2-->finish
//3-->delete

CREATE INDEX IDX_task_create_time on task(task_create_time);


insert into task values(NULL, 'simon', '12345678900', '12345678900',
'12345678900', 1, 'for test');

