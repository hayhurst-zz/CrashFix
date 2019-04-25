<?php

class CleanCommand extends CConsoleCommand
{
	public function getOptionHelp()
	{
		$help = "project=<name> appver=<ver> [limit=<number>]";

		$options = array();
		$options[] = $help;
		return $options;
	}

	public function run($args)
	{
		Yii::log("Entering the method run()", "info");
		echo "Entering the method run()\n";

		//echo "cmdline args = " . var_export($args, TRUE) . "\n";

		$keys = ["project", "appver", "limit"];
		$options = [];
		foreach ($args as $arg) {
			parse_str($arg, $output);

			foreach ($keys as $key) {
				if (array_key_exists($key, $output) && $output[$key] != "")
					$options[$key] = $output[$key];
			}
		}
		//echo "parsed options = " . var_export($options, TRUE) . "\n";

		if (array_key_exists("project", $options))
			$projectName = $options["project"];
		else
			throw new Exception('project name not found in parameters');

		if (array_key_exists("appver", $options))
			$appVer = $options["appver"];
		else
			throw new Exception('appver not found in parameters');

		$limit = 0;
		if (array_key_exists("limit", $options))
			$limit = intval($options["limit"]);
		if ($limit == 0)
			$limit = 10;
		echo "\nInput options: \n";
		echo "projectName = " . $projectName . "\n";
		echo "appver = " . $appVer . "\n";
		echo "limit = " . $limit . "\n";

		$project = Project::model()->find(
			'name=:name',
			array(':name' => $projectName)
		);
		if ($project === null)
			throw new Exception('Such a project name not found');
		$projectID = $project->getAttribute("id");
		echo "\nfind project $projectName, id = $projectID\n";

		$appVerion = AppVersion::model()->find(
			'version=:version',
			array(':version' => $appVer)
		);
		if ($appVerion === null)
			throw new Exception('Such a version name not found');
		$appVerionID = $appVerion->getAttribute("id");
		echo "find appver $appVer, id = $appVerionID\n";

		$criteria = new CDbCriteria;
		$criteria->compare('project_id', $projectID, false, 'AND');
		$criteria->compare('appversion_id', $appVerionID, false, 'AND');
		$count = CrashReport::model()->count($criteria);
		echo "CrashReport count = $count\n";

		$criteria = new CDbCriteria;
		$criteria->compare('project_id', $projectID, false, 'AND');
		$criteria->compare('appversion_id', $appVerionID, false, 'AND');
		$criteria->limit = $limit;
		$criteria->order = 'date_created ASC';
		$reports = CrashReport::model()->findAll($criteria);
		//$reports = array_merge($reports, $reports, $reports, $reports, $reports, $reports, $reports);
		$index = 1;
		$start_time = new DateTime;
		foreach ($reports as $report) {
			echo "[" . $index++ . "/" . sizeof($reports) . "] deleting CrashReport created on " . date("Y-m-d H:i:s", $report->date_created) . "\n";
			if(!$report->delete())
			{
				throw new CHttpException(404, 'The specified record doesn\'t exist in the database or could not be deleted.');
			}
			//sleep(1);

			if($index % 10 == 1)
			{
				$stop_time = new DateTime;
				echo $index - 1 ." reports deleted, elapsed time = " . $stop_time->diff($start_time)->format("%h:%i:%s\n");
			}
		}
		$stop_time = new DateTime;
		echo sizeof($reports) . " reports deleted, elapsed time = " . $stop_time->diff($start_time)->format("%h:%i:%s\n");

		// Success
		Yii::log("Leaving the method run()", "info");
		echo "\nLeaving the method run()\n";
		return 0;
	}
};
