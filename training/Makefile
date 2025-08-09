
# pip3 install tensorflow scikit-learn

validate:
	python3 validate_json.py --schema uc01-schema.json --input uc01-training.json

train:
	python3 uc01-train-model.py

test:
	python3 uc01-test-model.py --slicebitrate   700000
	python3 uc01-test-model.py --slicebitrate  1200000
	python3 uc01-test-model.py --slicebitrate  3000000
	python3 uc01-test-model.py --slicebitrate 12000000
	python3 uc01-test-model.py --slicebitrate 19000000

clean:
	rm -f uc01-model-on_air_classifier.keras uc01-scaler.joblib
