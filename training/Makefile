
# pip3 install tensorflow scikit-learn

validate:
	python3 validate_json.py --schema uc01-schema.json --input uc01-training.json

train:
	python3 uc01-train-model.py

test:
	python3 uc01-test-model.py

clean:
	rm -f uc01-model-on_air_classifier.keras uc01-scaler.joblib
